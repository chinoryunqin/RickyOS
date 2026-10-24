"""Run the actual reader refresh cadence: the 16-level turn's clean schedule with panel pages."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
READER = ROOT / 'src/activities/reader'


class ReaderRefreshCadenceTest(unittest.TestCase):
    def test_cleans_follow_the_interval_and_a_shorter_one_applies_at_once(self):
        reader = (READER / 'EpubReaderActivity.cpp').read_text()
        start = reader.index('      const auto refresh = ReaderUtils::consumeRefreshMode(pagesUntilFullRefresh);')
        end = reader.index('\n', reader.index('display.setNextGray16Profile(', start))
        turn = reader[start:end]
        stubs = {
            'CrossPointSettings.h': r'''
        #pragma once
        #include <climits>
        struct Settings {int frequency=10;int getRefreshFrequency()const{return frequency;}};
        inline Settings SETTINGS;
        ''',
            'GfxRenderer.h': r'''
        #pragma once
        struct HalDisplay {enum RefreshMode {FULL_REFRESH,HALF_REFRESH,FAST_REFRESH};};
        enum class DisplayRefreshContext {ContinuousReading,TextOnlyAntiAliasing};
        struct GfxRenderer {
         void displayBuffer(HalDisplay::RefreshMode,DisplayRefreshContext)const{}
         void displayBufferAsync(HalDisplay::RefreshMode,DisplayRefreshContext)const{}
         bool supportsTextOnlyCombinedBase()const{return false;}
         bool supportsReaderTransitions()const{return false;}
         bool canUseTextTransition()const{return false;}
         void waitRefreshComplete()const{}
         void displayGrayscaleBase(HalDisplay::RefreshMode,DisplayRefreshContext)const{}
        };
        ''',
        }
        program = r'''
        #include <cassert>
        #include <climits>
        #include <cstdint>
        #include "ReaderRefresh.h"
        struct Page {bool fill;bool hasPanelFill()const{return fill;}};
        struct {int profile=0;void setNextGray16Profile(int p){profile=p;}} display;
        struct Reader {
         int pagesUntilFullRefresh=0;bool lastPageHadPanelFill_=false;bool vertical=false;
         // Profile 2 is a full GC16 clean; 1 is an ordinary reader turn.
         int turn(bool fill){
          Page current{fill};Page* page=&current;
        ''' + turn + r'''
          return display.profile;
         }
        };
        int main(){
         uint32_t seed=12345;
         const auto next=[&]{seed=seed*1103515245u+12345u;return seed>>16;};
         for(int interval:{5,10,15,30}){
          SETTINGS.frequency=interval;
          Reader reader;reader.pagesUntilFullRefresh=interval;  // Opening a book.
          bool previousFill=false;int sinceClean=0,scheduled=0,panels=0;
          for(int n=0;n<2000;++n){
           const bool fill=next()%100<12;
           const bool panelTurn=fill||previousFill;previousFill=fill;
           ++sinceClean;
           if(reader.turn(fill)==2){
            // A scheduled clean is never closer than the interval to the previous clean of
            // any kind (panel page, page leaving one, or scheduled).
            if(!panelTurn){assert(sinceClean==interval);++scheduled;}else{++panels;}
            sinceClean=0;
           }else{
            assert(!panelTurn);
           }
           assert(sinceClean<interval);  // Never overdue either.
          }
          assert(scheduled>0 && panels>0);
         }
         // An interval shortened in the control center applies to the open book at once,
         // including from "never".
         for(int before:{INT_MAX,30}){
          SETTINGS.frequency=before;Reader reader;reader.pagesUntilFullRefresh=before;
          for(int n=0;n<3;++n)assert(reader.turn(false)==1);
          SETTINGS.frequency=10;int pages=0;
          while(reader.turn(false)!=2){++pages;assert(pages<10);}
         }
        }
        '''
        with tempfile.TemporaryDirectory(prefix='pico-refresh-cadence-') as folder:
            work = Path(folder)
            for name, text in stubs.items():
                (work / name).write_text(text)
            (work / 'test.cpp').write_text(program)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-DFREEINK_DEVICE_READPICO=1', '-I', str(work), '-I', str(READER),
                            str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
            subprocess.run([str(work / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
