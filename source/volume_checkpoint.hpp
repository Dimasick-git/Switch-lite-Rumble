#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace ryz::rumble {
struct VolumeCheckpoint { int speaker=0; float master=1; bool mute=false; };
// No scanf/printf floating-point conversion: newlib's dtoa/strtod paths
// require a newlib heap, which this Stratosphere module does not initialize.
inline bool ParseCheckpointUInt(const char* text,const char* key,unsigned base,std::uint32_t& out) {
    const size_t length=std::strlen(key);
    for (const char* line=text; line && *line;) {
        if (std::strncmp(line,key,length)==0 && line[length]=='=') {
            const char* p=line+length+1; std::uint32_t value=0; bool any=false;
            for (;*p && *p!='\r' && *p!='\n';++p) {
                unsigned digit=*p>='0'&&*p<='9'?unsigned(*p-'0'):
                    *p>='A'&&*p<='F'?unsigned(*p-'A'+10):
                    *p>='a'&&*p<='f'?unsigned(*p-'a'+10):base;
                if (digit>=base || value>(UINT32_MAX-digit)/base) return false;
                value=value*base+digit; any=true;
            }
            if (!any) return false;
            out=value; return true;
        }
        const char* next=std::strchr(line,'\n'); line=next?next+1:nullptr;
    }
    return false;
}
inline bool ParseLegacyMaster(const char* text,float& out) {
    for (const char* line=text;line&&*line;) {
        if (std::strncmp(line,"master=",7)==0) {
            const char* p=line+7;
            if (*p<'0'||*p>'3') return false;
            unsigned whole=unsigned(*p++-'0'),fraction=0,divisor=1;
            if (*p=='.') {
                ++p; unsigned digits=0;
                while (*p>='0'&&*p<='9') {
                    if (++digits>8) return false;
                    fraction=fraction*10+unsigned(*p++-'0'); divisor*=10;
                }
                if (!digits) return false;
            }
            if (*p && *p!='\r' && *p!='\n') return false;
            if (whole==3 && fraction) return false;
            out=whole+static_cast<float>(fraction)/divisor; return true;
        }
        const char* next=std::strchr(line,'\n');line=next?next+1:nullptr;
    }
    return false;
}
inline bool DecodeVolumeCheckpoint(const char* text,int maximum,VolumeCheckpoint& out) {
    std::uint32_t speaker=0,mute=0,bits=0;
    if (maximum<0 || !ParseCheckpointUInt(text,"speaker",10,speaker) || speaker>unsigned(maximum) ||
        !ParseCheckpointUInt(text,"mute",10,mute) || mute>1) return false;
    float master=0;
    if (ParseCheckpointUInt(text,"master_bits",16,bits)) {
        static_assert(sizeof(master)==sizeof(bits));std::memcpy(&master,&bits,sizeof(master));
    } else {
        // A malformed new record must not fall back to a different old field.
        if (std::strstr(text,"master_bits=") || !ParseLegacyMaster(text,master)) return false;
    }
    if (!std::isfinite(master) || master<0 || master>3) return false;
    out={static_cast<int>(speaker),master,mute!=0};return true;
}
inline bool EncodeVolumeCheckpoint(char* out,size_t capacity,const VolumeCheckpoint& value) {
    if (!out || capacity<64 || value.speaker<0 || !std::isfinite(value.master) || value.master<0 || value.master>3) return false;
    char* p=out;
    const auto append=[&](const char* text) { while (*text) *p++=*text++; };
    append("speaker=");
    char digits[12];unsigned count=0,n=static_cast<unsigned>(value.speaker);
    do {digits[count++]=char('0'+n%10);n/=10;} while(n);
    while(count) *p++=digits[--count];
    append("\nmaster_bits=");std::uint32_t bits=0;std::memcpy(&bits,&value.master,sizeof(bits));
    constexpr char Hex[]="0123456789ABCDEF";
    for(int shift=28;shift>=0;shift-=4)*p++=Hex[(bits>>shift)&15];
    append("\nmute=");*p++=value.mute?'1':'0';*p++='\n';*p=0;return true;
}
}
