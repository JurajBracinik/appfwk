/**
 * @file Application.cpp Application implementataion
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appfwk/Issues.hpp"
#include "appfwk/appinfo/InfoNljs.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "rcif/cmd/Nljs.hpp"
#include "rcif/runinfo/InfoNljs.hpp"

#include "logging/Logging.hpp"

#include "coremanager/CoreManager.hpp"
#include "coredal/DaqApplication.hpp"
#include "coredal/VirtualHost.hpp"
#include "coredal/ProcessingResource.hpp"

#include <string>
#include <unistd.h>


namespace dunedaq {
namespace appfwk {

Application::Application(std::string appname, std::string partition, std::string cmdlibimpl, std::string opmonlibimpl, std::string confimpl)
  : NamedObject(appname)
  , m_partition(partition)
  , m_info_mgr(opmonlibimpl)
  , m_state("NONE")
  , m_busy(false)
  , m_error(false)
  , m_initialized(false)
  , m_mod_mgr(appname)
{
  m_runinfo.running = false;
  m_runinfo.runno = 0;
  m_runinfo.runtime = 0;

  m_fully_qualified_name = partition + "." + appname;
  m_cmd_fac = cmdlib::make_command_facility(cmdlibimpl);

  if (confimpl.find("oksconfig:") != std::string::npos) {
    TLOG() << "Application: cmdlibimpl=<" << cmdlibimpl
           << "> confimpl=<" << confimpl << "> using OKS for configuration";
    m_confdb = new oksdbinterfaces::Configuration(confimpl);
    TLOG() << "Application loaded OKS configuration";
    m_conf_fac = nullptr;

    m_oksFile = confimpl.substr(9);
    auto app = m_confdb->get<dunedaq::coredal::DaqApplication>(appname);
    if (app == nullptr) {
      throw AppNotFound(ERS_HERE, appname);
    }
    // Check that resources used by modules exist in the host
    auto host = app->get_runs_on();
    std::set<const dunedaq::coredal::HostComponent*> host_resources;
    for (auto resource : host->get_uses()) {
      host_resources.insert(resource);
    }
    for (auto resource : app->get_used_hostresources()) {
      if (host_resources.find(resource) == host_resources.end()) {
        throw ApplicationResourceMismatch(ERS_HERE, get_name(), resource->UID());
      }
    }
    // Configure the CoreManager now before any threads get started
    coremanager::CoreManager::get()->configure(app);
    TLOG() << "Application: " << coremanager::CoreManager::get()->affinityString();
  }
  else {
    TLOG() << "Application: confimpl=<" << confimpl << "> using JSON for configuration";
    m_conf_fac = appfwk::make_conf_facility(confimpl);
    m_confdb = nullptr;
  }
}

void
Application::init()
{
  TLOG() << "Application: " << coremanager::CoreManager::get()->affinityString();
  m_cmd_fac->set_commanded(*this, get_name());
  m_info_mgr.set_provider(*this);
  // Add partition id as tag
  m_info_mgr.set_tags({ { "partition_id", m_partition } });

  if (m_conf_fac != nullptr) {
    // load the init params and init the app
    dataobj_t init_data = m_conf_fac->get_data(get_name(), "init", "");
    m_mod_mgr.initialize(init_data);
  }
  if (m_confdb != nullptr) {
    // pass the whole OKS DB to the module manager
    TLOG() << "Application::init() initialising module manager";
    m_mod_mgr.initialize(m_partition, m_confdb, m_oksFile);
  }
  set_state("INITIAL");
  m_initialized = true;
}

void
Application::run(std::atomic<bool>& end_marker)
{
  if (!m_initialized) {
    throw ApplicationNotInitialized(ERS_HERE, get_name());
  }

  setenv("DUNEDAQ_OPMON_INTERVAL", "10", 0);
  setenv("DUNEDAQ_OPMON_LEVEL", "1", 0);

  std::stringstream s1(getenv("DUNEDAQ_OPMON_INTERVAL"));
  std::stringstream s2(getenv("DUNEDAQ_OPMON_LEVEL"));
  uint32_t interval = 0; // NOLINT(build/unsigned)
  uint32_t level = 0;    // NOLINT(build/unsigned)
  s1 >> interval;
  s2 >> level;

  m_info_mgr.start(interval, level);
  m_cmd_fac->run(end_marker);
  m_info_mgr.stop();
  m_mod_mgr.cleanup();
}

void
Application::execute(const dataobj_t& cmd_data)
{

  auto rc_cmd = cmd_data.get<rcif::cmd::RCCommand>();
  std::string cmdname = rc_cmd.id;
  if (!is_cmd_valid(cmd_data)) {
    throw InvalidCommand(ERS_HERE, cmdname, get_state(), m_error.load(), m_busy.load());
  }

  m_busy.store(true);

  if (cmdname == "start") {
    auto cmd_obj = rc_cmd.data.get<cmd::CmdObj>();

    for (const auto& addressed : cmd_obj.modules) {
      dataobj_t startpars = addressed.data;
      auto rc_startpars = startpars.get<rcif::cmd::StartParams>();
      m_runinfo.runno = rc_startpars.run;
      break;
    }

    m_run_start_time = std::chrono::steady_clock::now();
    ;
    m_runinfo.running = true;
    m_runinfo.runtime = 0;
  } else if (cmdname == "stop") {
    m_runinfo.running = false;
    m_runinfo.runno = 0;
    m_runinfo.runtime = 0;
  }

  try {
    dataobj_t params;
    if (cmdname == "conf" && m_conf_fac != nullptr) {
	//std::string uri = rc_cmd.data;
	std::string uri = "";
      // load the conf params
      params = m_conf_fac->get_data(get_name(), cmdname, uri); 
    }
    else {
      params = rc_cmd.data;
    }
	  
    m_mod_mgr.execute(get_state(), cmdname, params);
    m_busy.store(false);
    if (rc_cmd.exit_state != "ANY")
      set_state(rc_cmd.exit_state);
  } catch (ers::Issue& ex) {
    m_busy.store(false);
    m_error.store(true);
    throw;
  }
}

void
Application::gather_stats(opmonlib::InfoCollector& ci, int level)
{
  appinfo::Info ai;
  ai.state = get_state();
  ai.busy = m_busy.load();
  ai.error = m_error.load();

  char hostname[256];
  auto res = gethostname(hostname, 256);
  if ( res < 0 ) ai.host = "Unknown";
  else ai.host = std::string(hostname);
  
  opmonlib::InfoCollector tmp_ci;

  tmp_ci.add(ai);

  if (ai.state == "RUNNING" || ai.state == "READY") {
    auto now = std::chrono::steady_clock::now();
    m_runinfo.runtime = std::chrono::duration_cast<std::chrono::seconds>(now - m_run_start_time).count();
  }
  tmp_ci.add(m_runinfo);

  if (level == 0) {
    // give only generic application info
  } else if (ai.state != "NONE" && ai.state != "INITIAL") {
    try {
      m_mod_mgr.gather_stats(tmp_ci, level);
    } catch (ers::Issue& ex) {
      ers::error(ex);
    }
  }
  ci.add(m_fully_qualified_name, tmp_ci);
}

bool
Application::is_cmd_valid(const dataobj_t& cmd_data)
{
  if (m_busy.load() || m_error.load())
    return false;

  std::string state = get_state();
  std::string entry_state = cmd_data.get<rcif::cmd::RCCommand>().entry_state;
  if (entry_state == "ANY" || state == entry_state)
    return true;

  return false;
}

} // namespace appfwk
} // namespace dunedaq
