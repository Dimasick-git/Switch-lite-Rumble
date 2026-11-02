#include "../source/config_value.hpp"
#include <cassert>
#include <initializer_list>
using ryz::rumble::ConfigNumber;
int main() {
    const char* spaced="\xEF\xBB\xBF[rumble]\r\n enabled = 0\r\n game_volume_buttons\t=\tfalse ; test\r\ngain_percent = 315 # value\n[other]\nenabled=1\n";
    assert(ConfigNumber(spaced,"rumble","enabled",1)==0);
    assert(ConfigNumber(spaced,"rumble","game_volume_buttons",1)==0);
    assert(ConfigNumber(spaced,"rumble","gain_percent",200)==315);
    assert(ConfigNumber(spaced,"other","enabled",0)==1);
    assert(ConfigNumber(spaced,"rumble","gain",2)==2);
    assert(ConfigNumber("[rumble]\nenabled_extra=0","rumble","enabled",1)==1);
    assert(ConfigNumber("[rumble]\nenabled = true","rumble","enabled",0)==1);
    for(const char* text:{"", "[rumble]\nenabled=", "[rumble]\nenabled=-1", "[rumble]\nenabled=4294967296", "[rumble]\nenabled=falsejunk"})
        assert(ConfigNumber(text,"rumble","enabled",7)==7);
}
