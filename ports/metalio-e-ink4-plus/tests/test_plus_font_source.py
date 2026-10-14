"""Exercise actual source changes, host privacy and repaint suppression."""
from pathlib import Path
import subprocess
import tempfile

root = Path('firmware/rickyos-plus-1.2/upstream')
source = (root / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
start = source.index('void FontDownloadActivity::updateDownloadSource(')
end = source.index('\nFontDownloadActivity::DownloadResult', start)
methods = source[start:end]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "FontDownloadSource.h"
#define LOG_INF(...) ((void)0)
#define tr(id) #id
uint32_t millis(){return 42;}
struct RenderLock {template<class T> explicit RenderLock(T&){} };
struct FontDownloadActivity {
 char downloadSourceHost_[256]{}; uint32_t lastProgressRefreshMs_=0; int frames=0;
 void requestUpdateAndWait(){++frames;}
 void updateDownloadSource(const char*);
 const char* downloadSourceLabel() const;
};
''' + methods + r'''
int main(){
 FontDownloadActivity activity;
 activity.updateDownloadSource(nullptr); assert(activity.frames==0);
 activity.updateDownloadSource("RICKYOS-1309217881.COS.AP-GUANGZHOU.MYQCLOUD.COM");
 assert(activity.frames==1 && activity.lastProgressRefreshMs_==42);
 assert(std::string(activity.downloadSourceLabel())=="STR_FONT_SOURCE_COS");
 activity.updateDownloadSource("rickyos-1309217881.cos.ap-guangzhou.myqcloud.com");
 assert(activity.frames==1); // Same source/new sizes do not force another flash.
 activity.updateDownloadSource("cdn.jsdelivr.net");
 assert(activity.frames==2 && std::string(activity.downloadSourceLabel())=="STR_FONT_SOURCE_JSDELIVR");
 activity.updateDownloadSource("fastly.jsdelivr.net");
 assert(std::string(activity.downloadSourceLabel())=="STR_FONT_SOURCE_FASTLY");
 activity.updateDownloadSource("github.com");
 assert(std::string(activity.downloadSourceLabel())=="STR_FONT_SOURCE_GITHUB");
 activity.updateDownloadSource("release-assets.githubusercontent.com");
 assert(std::string(activity.downloadSourceLabel())=="STR_FONT_SOURCE_GITHUB");
 activity.updateDownloadSource("user:private@cdn.jsdelivr.net");
 assert(std::string(activity.downloadSourceHost_)=="cdn.jsdelivr.net");
 activity.updateDownloadSource("cdn.jsdelivr.net.evil.test");
 assert(std::string(activity.downloadSourceLabel())=="cdn.jsdelivr.net.evil.test");
 activity.updateDownloadSource("unknown.test?secret=yes");
 assert(std::string(activity.downloadSourceHost_)=="unknown.test");
 activity.updateDownloadSource(std::string(1000,'x').c_str());
 assert(std::strlen(activity.downloadSourceHost_)==255);
 activity.updateDownloadSource(""); assert(activity.downloadSourceHost_[0]==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'src/network'),
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('Font source: actual state/labels, mirror changes, GitHub asset redirect, host-only bounded storage and repeated-source suppression passed')
