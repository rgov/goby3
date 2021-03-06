// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef APPLICATIONBASE320161120H
#define APPLICATIONBASE320161120H

#include <iostream>
#include <csignal>
#include <chrono>

#include <boost/format.hpp>

#include "goby/common/exception.h"
#include "goby/common/configuration_reader.h"

#include "goby/common/protobuf/app3.pb.h"

#include "goby/common/logger.h"
#include "goby/common/time3.h"

#include "core_helpers.h"

namespace goby
{        
    /// \brief Run a Goby application 
    /// blocks caller until ```__run()``` returns
    /// \param argc same as ```int main(int argc, char* argv)```
    /// \param argv same as ```int main(int argc, char* argv)```
    /// \tparam App ApplicationBase3 subclass
    /// \tparam StateMachine boost::statechart::state_machine to pass a copy of App to and then initiate. If omitted, no state machine is used.
    /// \return same as ```int main(int argc, char* argv)```
    template<typename App>
	int run(int argc, char* argv[]);

    namespace common
    {
	using NullStateMachine = int;

	namespace internal
	{
	    // no state machine
	    template<typename App>
		void __run_core(App& app,
				typename std::enable_if<std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type* = 0)
	    {}
	    
	    // state machine
	    template<typename App>
		void __run_core(App& app, typename std::enable_if<!std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type* = 0);

	    // no state machine
	    template<typename App>
		void __quit_core(App& app,
				typename std::enable_if<std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type* = 0)
	    {}
	    
	    // state machine
	    template<typename App>
		void __quit_core(App& app, typename std::enable_if<!std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type* = 0);

        }
    }
    
    namespace common
    {
	template<typename Config, typename StateMachine = NullStateMachine>
            class ApplicationBase3
        {
        public:
            ApplicationBase3(bool check_required_configuration = true);
            virtual ~ApplicationBase3() { }

            using StateMachineType = StateMachine;	
        protected:
            /// \brief Runs continously until quit() is called
            virtual void run() = 0;

            /// \brief Requests a clean (return 0) exit.
            virtual void quit() { alive_ = false; }
            
            /// \brief Accesses configuration object passed at launch
            Config& app_cfg() { return cfg_; }

	    /// \brief Access the state machine if available (cannot be accessed from the constructor)
	    StateMachine& state_machine()
	    {
		static_assert(!std::is_same<StateMachine, NullStateMachine>(), "No state machine defined for this Goby application");
		if(state_machine_)
		    return *state_machine_;
		else
		    throw(goby::Exception("State machine not available in the constructor."));
	    }
	    
          private:
            template<typename App>
            friend int ::goby::run(int argc, char* argv[]);

            // main loop that exits on disconnect. called by goby::run()
            void __run();
            
            void __set_application_name(const std::string& s)
            { cfg_.mutable_app()->set_name(s); }
            
	    
          private:
                
            // copies of the "real" argc, argv that are used
            // to give ApplicationBase3 access without requiring the subclasses of
            // ApplicationBase3 to pass them through their constructors
            static int argc_;
            static char** argv_;

            Config cfg_;
            
            bool alive_;            
            std::vector<std::unique_ptr<std::ofstream> > fout_;

            // set state machine after construction
            template<typename App>
            friend void ::goby::common::internal::__run_core(App& app,
							     typename std::enable_if<!std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type*);            
            template<typename App>
            friend void ::goby::common::internal::__quit_core(App& app,
							     typename std::enable_if<!std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type*);            
            std::unique_ptr<StateMachine> state_machine_;
	    
        };
    }
    
}

template<typename Config, typename StateMachine>
    int goby::common::ApplicationBase3<Config, StateMachine>::argc_ = 0;

template<typename Config, typename StateMachine>
    char** goby::common::ApplicationBase3<Config, StateMachine>::argv_ = 0;


    
template<typename Config, typename StateMachine>
    goby::common::ApplicationBase3<Config, StateMachine>::ApplicationBase3(bool check_required_configuration)
: alive_(true)
{
    using goby::glog;
    using namespace goby::common::logger;
    
    //
    // read the configuration
    //
    boost::program_options::options_description od("Allowed options");
    boost::program_options::variables_map var_map;
    try
    {
        std::string application_name;
        common::ConfigReader::read_cfg(argc_, argv_, &cfg_, &application_name, &od, &var_map, check_required_configuration);
        
        __set_application_name(application_name);
        // incorporate some parts of the AppBaseConfig that are common
        // with gobyd (e.g. Verbosity)
        merge_app_base_cfg(cfg_.mutable_app(), var_map);

    }
    catch(common::ConfigException& e)
    {
        // output all the available command line options
        if(e.error())
        {
            std::cerr << od << "\n";
            std::cerr << "Problem parsing command-line configuration: \n"
                      << e.what() << "\n";
        }
        throw;
    }
    
    // set up the logger
    glog.set_name(cfg_.app().name());
    glog.add_stream(static_cast<common::logger::Verbosity>(cfg_.app().glog_config().tty_verbosity()), &std::cout);

   if(cfg_.app().glog_config().show_gui())
       glog.enable_gui();

   fout_.resize(cfg_.app().glog_config().file_log_size());
   for(int i = 0, n = cfg_.app().glog_config().file_log_size(); i < n; ++i)
   {
       using namespace boost::posix_time;

       boost::format file_format(cfg_.app().glog_config().file_log(i).file_name());
       file_format.exceptions( boost::io::all_error_bits ^ ( boost::io::too_many_args_bit | boost::io::too_few_args_bit)); 

       std::string file_name = (file_format % to_iso_string(second_clock::universal_time()) % cfg_.app().name()).str();
       std::string file_symlink = (file_format % "latest" % cfg_.app().name()).str();

       glog.is(VERBOSE) &&
           glog << "logging output to file: " << file_name << std::endl;

       fout_[i].reset(new std::ofstream(file_name.c_str()));
       
       if(!fout_[i]->is_open())           
           glog.is(DIE) && glog << die << "cannot write glog output to requested file: " << file_name << std::endl;

       remove(file_symlink.c_str());
       int result = symlink(canonicalize_file_name(file_name.c_str()), file_symlink.c_str());
       if(result != 0)
           glog.is(WARN) && glog << "Cannot create symlink to latest file. Continuing onwards anyway" << std::endl;
        
       
       glog.add_stream(cfg_.app().glog_config().file_log(i).verbosity(), fout_[i].get());
   } 
   
   
    if(!cfg_.app().IsInitialized())
        throw(common::ConfigException("Invalid base configuration"));
    
    glog.is(DEBUG1) && glog << "App name is " << cfg_.app().name() << std::endl;
    glog.is(DEBUG2) && glog << "Configuration is: " << cfg_.DebugString() << std::endl;

    // set up simulation time
    if(cfg_.app().simulation().time().use_sim_time())
    {
        goby::time::SimulatorSettings::using_sim_time = true;
        goby::time::SimulatorSettings::warp_factor = cfg_.app().simulation().time().warp_factor();
        if(cfg_.app().simulation().time().has_reference_microtime())
            goby::time::SimulatorSettings::reference_time = cfg_.app().simulation().time().reference_microtime()*boost::units::si::micro*boost::units::si::seconds;
    }
}

template<typename Config, typename StateMachine>
    void goby::common::ApplicationBase3<Config, StateMachine>::__run()
{
    // block SIGWINCH (change window size) in all threads
    sigset_t signal_mask;
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGWINCH);
    pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);

    // continue to run while we are alive (quit() has not been called)
    while(alive_)
    {
        this->run();
    }
}


template<typename App>
void goby::common::internal::__run_core(App& app,
					typename std::enable_if<!std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type*)
{
    app.state_machine_.reset(new typename App::StateMachineType(app));
    app.state_machine_->initiate();
}    

template<typename App>
void goby::common::internal::__quit_core(App& app,
					typename std::enable_if<!std::is_same<typename App::StateMachineType, goby::common::NullStateMachine>::value>::type*)
{
    app.state_machine_->terminate();
}    


template<typename App>
    int goby::run(int argc, char* argv[])
{    
    // avoid making the user pass these through their Ctor...
    App::argc_ = argc;
    App::argv_ = argv;
    
    try
    {
	App app;
	goby::common::internal::__run_core<App>(app);
	app.__run();
	goby::common::internal::__quit_core<App>(app);
    }
    catch(goby::common::ConfigException& e)
    {
        // no further warning as the ApplicationBase3 Ctor handles this
        return 1;
    }
    catch(std::exception& e)
    {
        // some other exception
        std::cerr << "ApplicationBase3:: uncaught exception: " << e.what() << std::endl;
        return 2;
    }

    return 0;
}


#endif
