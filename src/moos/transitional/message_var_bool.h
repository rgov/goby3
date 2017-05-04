// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef MESSAGE_VAR_BOOL20100317H
#define MESSAGE_VAR_BOOL20100317H

#include "message_var.h"

namespace goby
{
namespace transitional
{   
    class DCCLMessageVarBool : public DCCLMessageVar
    {
      public:
        DCCLType type() const { return dccl_bool; }
        
      private:
        void initialize_specific()
        { }
        

        void get_display_specific(std::stringstream& ss) const
        { }        

    };
}
}

#endif
