#ifndef Group20170807H
#define Group20170807H

#include <string>

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define H1(s,i,x)   (x*65599u+(uint8_t)s[(i)<strlen(s)?strlen(s)-1-(i):strlen(s)])
#define H4(s,i,x)   H1(s,i,H1(s,i+1,H1(s,i+2,H1(s,i+3,x))))
#define H16(s,i,x)  H4(s,i,H4(s,i+4,H4(s,i+8,H4(s,i+12,x))))
#define H64(s,i,x)  H16(s,i,H16(s,i+16,H16(s,i+32,H16(s,i+48,x))))
#define H256(s,i,x) H64(s,i,H64(s,i+64,H64(s,i+128,H64(s,i+192,x))))

#define HASH(s)    ((uint32_t)(H256(s,0,0)^(H256(s,0,0)>>16)))

namespace goby
{
    class Group
    {
    public:
        constexpr Group(const char* c = "")
            : i_(HASH(c)), c_(c)  { }
        constexpr Group(uint32_t i) : i_(i) { }
	
        constexpr uint32_t hash() const { return i_; }
        constexpr const char* c_str() const { return c_; }
        
        operator std::string() const
        {            
            if(c_ != nullptr) return std::string(c_);
            else return std::to_string(i_);
         }

        operator int32_t() const {
            if(i_ > std::numeric_limits<int32_t>::max())
                throw(std::out_of_range("Group integer greater than int32_t"));
            
            return static_cast<int32_t>(i_);
        }
    

        
    protected:
	void set_c_str(const char* c)
        {
            c_ = c;
            i_ = HASH(c);
        }
		
    private:
        uint32_t i_{0};
        const char* c_{nullptr};
        
        bool hash_set_{false};
        std::size_t hash_{0};
    };
    
    inline bool operator==(const Group& a, const Group& b)
    { return a.hash() == b.hash(); }
    
    //    inline bool operator<(const Group& a, const Group& b)
    //{ return std::string(a) < std::string(b); }
    
    
    template<const Group& group>
        void check_validity()
    {
        static_assert((group.hash() != 0) || (group.c_str()[0] != '\0'), "goby::Group must have non-zero length string or non-zero integer value.");
    }

    inline void check_validity_runtime(const Group& group)
    {
        // currently no-op - InterVehicleTransporterBase allows empty groups
        // TODO - check if there's anything we can check for here
    }


    class DynamicGroup : public Group
    {
    public:
    DynamicGroup(const std::string& s) : s_(new std::string(s))
    {
	Group::set_c_str(s_->c_str());
    }
    DynamicGroup(int i) : Group(i) { }

    private:
	std::unique_ptr<const std::string> s_;
    };
    
}


#endif
