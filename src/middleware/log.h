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

#ifndef Log20171127H
#define Log20171127H

#include <cstdint>
#include <boost/bimap.hpp>
#include <boost/crc.hpp>     

#include "goby/common/logger.h"
#include "goby/common/exception.h"
#include "group.h"

namespace goby
{
    template<int Bytes> struct uint {};
    template<> struct uint<1> { typedef std::uint8_t type; };
    template<> struct uint<2> { typedef std::uint16_t type; };
    template<> struct uint<4> { typedef std::uint32_t type; };
    template<> struct uint<8> { typedef std::uint64_t type; };
    
    class LogEntry
    {
    private:
        
        static constexpr int size_bytes_{4};
        static constexpr int scheme_bytes_{2};
        static constexpr int group_bytes_{2};
        static constexpr int type_bytes_{2};
        static constexpr int crc_bytes_ {4};
        static constexpr uint<scheme_bytes_>::type scheme_group_index_ { 0xFFFF };
        static constexpr uint<scheme_bytes_>::type scheme_type_index_ { 0xFFFE };
        
    public:
    LogEntry(const std::vector<unsigned char>& data, int scheme, const std::string& type, const Group& group)
        : data_(data),
            scheme_(scheme),
            type_(type),
            group_(std::string(group))
            { }
            
    LogEntry() : group_("") { }

        template <typename Stream>
            void parse(Stream* s)
        {
            using namespace goby::common::logger;
            using goby::glog;
            
            auto old_except_mask = s->exceptions();
            s->exceptions(std::ios::failbit | std::ios::badbit | std::ios::eofbit);

            uint<scheme_bytes_>::type scheme(0);
            do
            {
            
                char next_char = s->peek();
                if(next_char != magic_[0])
                {
                    glog.is(WARN) && glog << "Next character [" << next_char << "] is not the start of the expected magic word [" << magic_ << "]. Seeking until next magic word." << std::endl;
                }

                std::string magic_read(magic_.size(), '\0');
                int discarded = 0;

                while(magic_read != magic_)
                {
                    while(s->peek() != magic_[0])
                    {
                        ++discarded;
                        s->get();
                    }
                    s->read(&magic_read[0], magic_.size());
                }

                if(discarded != 0)
                    glog.is(WARN) && glog << "Found next magic word after skipping " << discarded << " bytes" << std::endl;

                boost::crc_32_type crc;
                crc.process_bytes(&magic_read[0], magic_.size());
                
                auto size(read_one<uint<size_bytes_>::type>(s, &crc));         
                auto fixed_field_size = scheme_bytes_ + group_bytes_ + type_bytes_ + crc_bytes_;

                if(size < fixed_field_size)
                    throw(goby::Exception("Invalid size read: " + std::to_string(size) + " as message must be at least " + std::to_string(fixed_field_size) + " bytes long"));
                
                auto data_size = size - fixed_field_size;
                glog.is(DEBUG2) && glog << "Reading entry of " << size << " bytes (" << data_size << " bytes data)" << std::endl;
            
                scheme = read_one<uint<scheme_bytes_>::type>(s, &crc);
                auto group_index(read_one<uint<group_bytes_>::type>(s, &crc));
                auto type_index(read_one<uint<type_bytes_>::type>(s, &crc));
                
                data_.resize(data_size);
                s->read(reinterpret_cast<char*>(&data_[0]), data_size);
                crc.process_bytes(&data_[0], data_.size());
                
                auto calculated_crc = crc.checksum();
                
                auto given_crc(read_one<uint<crc_bytes_>::type>(s));

                if(calculated_crc != given_crc)
                {
                    data_.clear();
                    throw(goby::Exception("Invalid CRC on packet: given: " + std::to_string(given_crc) + ", calculated: " + std::to_string(calculated_crc)));
                }

                if(scheme == scheme_group_index_)
                {
                    std::string group(data_.begin(), data_.end());
                    glog.is(DEBUG1) && glog << "Mapping group [" << group << "] to index: " << group_index << std::endl;
                    groups_.left.insert({ group, group_index });
                    data_.clear();
                }
                else if(scheme == scheme_type_index_)
                {
                    std::string type(data_.begin(), data_.end());
                    glog.is(DEBUG1) && glog << "Mapping type [" << type << "] to index: " << type_index << std::endl;
                    types_.left.insert({ type, type_index });
                    data_.clear();
                }
                else
                {
                    scheme_ = scheme;

                    std::string type = "_unknown" + std::to_string(type_index) + "_";  
                    auto type_it = types_.right.find(type_index);
                    if(type_it != types_.right.end())
                        type = type_it->second;
                    else
                        glog.is(WARN) && glog << "No type entry in file for type index: " << type_index << std::endl;                    
                    type_ = type;

                    std::string group = "_unknown" + std::to_string(group_index) + "_";  
                    auto group_it = groups_.right.find(group_index);
                    if(group_it != groups_.right.end())
                        group = group_it->second;
                    else
                        glog.is(WARN) && glog << "No group entry in file for group index: " << group_index << std::endl;                    
                    group_ = goby::DynamicGroup(group);
                }   
            }
            while(scheme == scheme_group_index_ || scheme == scheme_type_index_);

            s->exceptions(old_except_mask);
        }
        
        // [GBY3][size: 4][scheme: 2][group: 2][type: 2][data][crc32: 4]
        // if scheme == 0xFFFF what follows is not data, but the string value for the group index
        // if scheme == 0xFFFE what follows is not data, but the string value for the group index
        template <typename Stream>
            void serialize(Stream* s)
        {
            auto old_except_mask = s->exceptions();
            s->exceptions(std::ios::failbit | std::ios::badbit | std::ios::eofbit);

            std::string group(group_);

            // insert indexing entry if the first time we saw this group
            if(groups_.left.count(group) == 0)
            {
                auto index = group_index_++;
                groups_.left.insert({ group, index });
                _serialize(s, scheme_group_index_,
                           index, 0, group.data(), group.size());
            }            
            if(types_.left.count(type_) == 0)
            {
                auto index = type_index_++;
                types_.left.insert({ type_, index });
                _serialize(s, scheme_type_index_,
                           0, index, type_.data(), type_.size());
            }
            
            auto group_index = groups_.left.at(group);
            auto type_index = types_.left.at(type_);

            // insert actual data
            _serialize(s, scheme_, group_index, type_index,
                       reinterpret_cast<const char*>(&data_[0]), data_.size());

            s->exceptions(old_except_mask);
        }
        
        const std::vector<unsigned char>& data() { return data_; }
        int scheme() { return scheme_; }
        const std::string& type() { return type_; }
        const Group& group() { return group_; }

        static void reset() 
        {
            groups_.clear();
            types_.clear();
            group_index_ = 1;
            type_index_ = 1;
        }
        
        
    private:

        template <typename Stream>
            void _serialize(Stream* s,
                            uint<scheme_bytes_>::type scheme,
                            uint<group_bytes_>::type group_index,
                            uint<type_bytes_>::type type_index,
                            const char* data,
                            int data_size)
        {
            std::string group_str(netint_to_string(group_index));
            std::string type_str(netint_to_string(type_index));
            std::string scheme_str(netint_to_string(scheme));

            uint<size_bytes_>::type size = scheme_bytes_ + group_bytes_ + type_bytes_ + data_size + crc_bytes_;
            std::string size_str(netint_to_string(size));

            auto header = magic_ + size_str + scheme_str + group_str + type_str;
            s->write(header.data(), header.size());
            s->write(data, data_size);

            boost::crc_32_type crc;
            crc.process_bytes(header.data(), header.size());
            crc.process_bytes(data, data_size);
            
            uint<crc_bytes_>::type cs(crc.checksum());
            std::string cs_str(netint_to_string(cs));
            s->write(cs_str.data(), cs_str.size());
        }
                          
        template <typename Unsigned, typename Stream>
            Unsigned read_one(Stream* s, boost::crc_32_type* crc = 0)
        {
            auto size = std::numeric_limits<Unsigned>::digits/8;
            std::string str(size, '\0');
            s->read(&str[0], size);
            if(crc) crc->process_bytes(&str[0], size);
            return string_to_netint<Unsigned>(str);
        }

        
        template<typename Unsigned>
            std::string netint_to_string(Unsigned u)
        {
            auto size = std::numeric_limits<Unsigned>::digits/8;
            std::string s(size, '\0');
            for(int i = 0; i < size; ++i)
                s[i] = (u >> (size-(i+1))*8) & 0xff;
            return s;
        }
        
        template<typename Unsigned>
            Unsigned string_to_netint(std::string s)
        {
            Unsigned u(0);
            auto size = std::numeric_limits<Unsigned>::digits/8;
            if(s.size() > size)
                s.erase(0, s.size() - size);
            if(s.size() < size)
                s.insert(0, size - s.size(), '\0');

            
            for(int i = 0; i < size; ++i)
                u |= (s[i] & 0xff) << ((size-(i+1))*8);
            return u;
        }
        
    private:
        std::vector<unsigned char> data_;
        int scheme_;
        std::string type_;
        DynamicGroup group_;

        
        
        static boost::bimap<std::string, uint<group_bytes_>::type> groups_;
        static uint<group_bytes_>::type group_index_;
        
        static boost::bimap<std::string, uint<type_bytes_>::type> types_;
        static uint<type_bytes_>::type type_index_;
        
        const std::string magic_ { "GBY3" };
    };
    

}


#endif
