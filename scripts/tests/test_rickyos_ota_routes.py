"""Exercise the real OTA check with A4/Pico manifests and a recording HTTPS seam."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]


class OtaRouteTest(unittest.TestCase):
    def test_own_board_cos_first_and_fallback(self):
        source = (ROOT/'src/network/OtaUpdater.cpp').read_text()
        check = method(source, 'OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate(')
        check += method(source, 'const std::string& OtaUpdater::getLatestVersion() const')
        program = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include "network/OtaUpdater.h"
#include "network/RickyOtaManifest.h"
#include "network/RickyOtaNotes.h"
namespace ricky_ota {inline constexpr char COS_ROOT_CA[]="cos-root";inline constexpr char ROOT_CA[]="site-root";}
namespace board_tag {
const char* running=ricky_ota::BOARD;
const char* boardName(){return running;}
size_t boardNameLen(){return strlen(running);}
}
template<class... T>void log(const char*,T...){}
#define LOG_INF(...) log(__VA_ARGS__)
#define LOG_ERR(...) log(__VA_ARGS__)
int scenario=0;
std::vector<std::string> calls;
namespace HttpDownloader {
template<class Sink>bool fetchVerifiedUrl(const char* url,Sink sink,const char* root){
 calls.emplace_back(url);
 bool cos=strstr(url,"myqcloud.com")!=nullptr;
 assert(root==(cos?ricky_ota::COS_ROOT_CA:ricky_ota::ROOT_CA));
 if(scenario==2 || (scenario==1 && cos))return false;
 bool notes=strstr(url,"ota-notes.txt")!=nullptr;
 if(notes && scenario==5)return false;
 std::string body;
 if(notes)body=std::string("1.2.5\n")+(cos?"COS notes":"Site notes")+"\n";
 else {
  body=std::string("{\"schema\":1,\"product\":\"RickyOS\",\"board\":\"")+ricky_ota::BOARD+"\",\"channel\":\"stable\",\"status\":\"";
  if(scenario==3)body+="no_update\"}";
  else body+="update_available\",\"version\":\"1.2.5\",\"file\":\"firmware/A4.bin\",\"bytes\":5993888,\"sha256\":\""+std::string(64,'a')+"\",\"approved\":true,\"hardwareAccepted\":true,\"chipId\":9,\"flashBytes\":16777216}";
  if(scenario==4)body.pop_back();
 }
 for(size_t i=0;i<body.size();i+=13){size_t n=std::min<size_t>(13,body.size()-i);if(!sink(reinterpret_cast<const uint8_t*>(body.data()+i),n))return false;}
 return true;
}
}
"""
        main = r"""
int main(){
 using U=OtaUpdater;
 U u;
 scenario=0;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::OK);
 assert(calls==std::vector<std::string>({ricky_ota::COS_MANIFEST_URL,ricky_ota::COS_NOTES_URL}));
 assert(u.getLatestVersion()=="1.2.5" && u.getOtaSize()==5993888);
 assert(u.getReleaseNotes().size()==1 && std::string(u.getReleaseNotes()[0].data())=="COS notes");
 scenario=1;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::OK);
 assert(calls==std::vector<std::string>({ricky_ota::COS_MANIFEST_URL,ricky_ota::MANIFEST_URL,ricky_ota::NOTES_URL}));
 assert(std::string(u.getReleaseNotes()[0].data())=="Site notes");
 scenario=2;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::HTTP_ERROR);
 assert(calls.size()==2 && u.getLatestVersion().empty() && u.getOtaSize()==0 && u.getReleaseNotes().empty());
 scenario=3;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::NO_UPDATE);
 assert(calls==std::vector<std::string>({ricky_ota::COS_MANIFEST_URL}));
 scenario=4;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::JSON_PARSE_ERROR);
 assert(calls.size()==1);
 scenario=5;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::OK);
 assert(u.getReleaseNotes().empty() && u.getLatestVersion()=="1.2.5");
 // Own-board check still rejects a different physical build before any request.
 for(const char* foreign:{"other",std::string_view(ricky_ota::BOARD)=="eego_a4"?"readpico":"eego_a4"}){
  board_tag::running=foreign;calls.clear();assert(u.checkForUpdate(U::Channel::Stable)==U::WRONG_DEVICE_ERROR);
  assert(calls.empty() && u.getLatestVersion().empty() && u.getOtaSize()==0);
 }
}
"""
        with tempfile.TemporaryDirectory(prefix='ricky-ota-routes-') as directory:
            d=Path(directory);(d/'ReleaseJsonParser.h').write_text('#pragma once\n#include <array>\nstruct ReleaseJsonParser {using ReleaseNote=std::array<char,97>;static constexpr size_t RELEASE_NOTE_SIZE=97, RELEASE_NOTE_COUNT_MAX=8;};\n')
            cpp=d/'test.cpp';exe=d/'test';cpp.write_text(program+check+main)
            for board in ('1','0'):
                subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                                '-I'+str(d),'-I'+str(ROOT/'src'),'-DFREEINK_DEVICE_EEGO_A4='+board,str(cpp),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
