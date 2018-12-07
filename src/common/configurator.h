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

#ifndef CONFIGURATOR_20181205H
#define CONFIGURATOR_20181205H

#include "goby/common/configuration_reader.h"
#include "goby/common/core_helpers.h"
#include "goby/common/protobuf/app3.pb.h"

namespace goby
{
namespace common
{
template <typename Config> class ConfiguratorInterface
{
  public:
    const Config& cfg() const { return cfg_; }

    // TODO: App3Config will eventually not be a Protobuf Message, just a C++ struct
    const goby::protobuf::App3Config& app3_configuration() const { return app3_configuration_; }
    virtual void validate() const {}
    virtual void handle_config_error(common::ConfigException& e) const
    {
        std::cerr << "Invalid configuration: " << e.what() << std::endl;
    }

    virtual std::string str() const = 0;

  protected:
    // Derived classes can modify these as needed in their constructor
    Config& mutable_cfg() { return cfg_; }
    goby::protobuf::App3Config& mutable_app3_configuration() { return app3_configuration_; }

  private:
    Config cfg_;
    goby::protobuf::App3Config app3_configuration_;
};

/// Implementation of ConfiguratorInterface for Google Protocol buffers
template <typename Config> class ProtobufConfigurator : public ConfiguratorInterface<Config>
{
  public:
    ProtobufConfigurator(int argc, char* argv[]);

  protected:
    // subclass should call to verify all required fields have been set
    virtual void validate() const override
    {
        common::ConfigReader::check_required_cfg(this->cfg());
    }

  private:
    virtual std::string str() const override { return this->cfg().DebugString(); }

    void handle_config_error(common::ConfigException& e) const override
    {
        std::cerr << "Invalid configuration: use --help and/or --example_config for more help: "
                  << e.what() << std::endl;
    }
};

template <typename Config>
ProtobufConfigurator<Config>::ProtobufConfigurator(int argc, char* argv[])
{
    //
    // read the configuration
    //
    Config& cfg = this->mutable_cfg();

    boost::program_options::variables_map var_map;
    try
    {
        std::string application_name;

        // we will check it later in validate()
        bool check_required_cfg = false;
        boost::program_options::options_description od{"Allowed options"};
        common::ConfigReader::read_cfg(argc, argv, &cfg, &application_name, &od, &var_map,
                                       check_required_cfg);

        cfg.mutable_app()->set_name(application_name);
        // incorporate some parts of the AppBaseConfig that are common
        // with gobyd (e.g. Verbosity)
        merge_app_base_cfg(cfg.mutable_app(), var_map);
    }
    catch (common::ConfigException& e)
    {
        handle_config_error(e);
        throw;
    }

    // TODO: convert to C++ struct app3 configuration format
    this->mutable_app3_configuration() = *cfg.mutable_app();
}

} // namespace common
} // namespace goby

#endif