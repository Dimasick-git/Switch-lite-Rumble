#pragma once
#include <cstdint>
#include <cstring>
namespace ryz::rumble {
inline unsigned ConfigNumber(const char* text,const char* section,const char* key,unsigned fallback) {
    if(std::strncmp(text,"\xEF\xBB\xBF",3)==0)text+=3;
    bool selected=false;
    const auto equal=[](const char* a,const char* end,const char* b) {
        return static_cast<std::size_t>(end-a)==std::strlen(b) && std::memcmp(a,b,end-a)==0;
    };
    for(const char* line=text;*line;) {
        const char* end=std::strchr(line,'\n');if(!end)end=line+std::strlen(line);
        const char* next=*end?end+1:end;
        while(line<end && (*line==' '||*line=='\t'||*line=='\r'))++line;
        if(line<end && *line=='[') {
            const char* close=line+1;while(close<end && *close!=']')++close;
            selected=close<end && equal(line+1,close,section);
        } else if(selected && line<end && *line!=';' && *line!='#') {
            const char* assignment=line;while(assignment<end && *assignment!='=')++assignment;
            const char* key_end=assignment;
            while(key_end>line && (key_end[-1]==' '||key_end[-1]=='\t'))--key_end;
            if(assignment<end && equal(line,key_end,key)) {
                const char* p=assignment+1;while(p<end && (*p==' '||*p=='\t'))++p;
                const char* value_end=p;while(value_end<end && *value_end!=';' && *value_end!='#')++value_end;
                while(value_end>p && (value_end[-1]==' '||value_end[-1]=='\t'||value_end[-1]=='\r'))--value_end;
                if(equal(p,value_end,"true"))return 1;
                if(equal(p,value_end,"false"))return 0;
                if(p==value_end)return fallback;
                unsigned value=0;
                for(;p<value_end;++p) {
                    if(*p<'0'||*p>'9'||value>(UINT32_MAX-unsigned(*p-'0'))/10)return fallback;
                    value=value*10+unsigned(*p-'0');
                }
                return value;
            }
        }
        line=next;
    }
    return fallback;
}
}
