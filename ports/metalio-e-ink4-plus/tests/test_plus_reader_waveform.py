"""Execute full/default and opt-in short reader waveform assembly, checking every pulse."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = ROOT / 'firmware/rickyos-plus-1.2/components/metalio_board/display'
assert 'display/waveforms/my_waveform_trim.c' in (DISPLAY.parent / 'CMakeLists.txt').read_text()
PROGRAM = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "waveforms/my_waveform.h"
extern const uint8_t my_gc_25_0_data[48][16][4];
extern const uint8_t my_gl_25_0_data[48][16][4];
static const EpdWaveformPhases* mode(int type) {
    for (int i=0; i<MY_WAVEFORM.num_modes; ++i)
        if (MY_WAVEFORM.mode_data[i]->type==type)
            return MY_WAVEFORM.mode_data[i]->range_data[0];
    assert(0); return 0;
}
static int action(const EpdWaveformPhases* p,int f,int to,int from) {
    return (p->luts[(f*16+to)*4+from/4]>>(6-2*(from%4)))&3;
}
int main(void) {
    my_waveform_init();
    assert(MY_WAVEFORM.num_modes==4); // Measurement mode never leaks into default builds.
    const EpdWaveformPhases* normal=mode(5);
    const EpdWaveformPhases* reader=mode(6);
    const EpdWaveformPhases* clean=mode(2);
    assert(reader->phase_times==normal->phase_times);
    assert(reader->phases==EXPECTED_READER_PHASES);
    assert(normal->phases==EXPECTED_PHASES);
#if !MY_WAVEFORM_TRIM
    assert(memcmp(clean->luts,my_gc_25_0_data,sizeof(my_gc_25_0_data))==0);
    int white_tick=0;
    for(int f=0; f<48; ++f) for(int to=0; to<16; ++to) for(int from=0; from<16; ++from) {
        int original=(my_gl_25_0_data[f][to][from/4]>>(6-2*(from%4)))&3;
        int assembled=action(normal,f,to,from);
        if(to==15 && from==15 && assembled!=original) {
            assert(original==0 && assembled==2); ++white_tick;
        } else assert(assembled==original);
    }
    assert(white_tick==1); // Original GL16 plus its pre-existing white maintenance pulse.
#endif
    for(int f=0; f<reader->phases; ++f) for(int gray=0; gray<16; ++gray)
        assert(action(reader,f,gray,gray)==0);
    unsigned char saved[48*16*4];
    memcpy(saved,reader->luts,(size_t)reader->phases*16*4);
    unsigned char saved_clean[48*16*4];
    memcpy(saved_clean,clean->luts,(size_t)clean->phases*16*4);
    my_waveform_init(); // Boot assembly is deterministic; no additive state.
    assert(memcmp(saved,reader->luts,(size_t)reader->phases*16*4)==0);
    assert(memcmp(saved_clean,clean->luts,(size_t)clean->phases*16*4)==0);
    // Important limit: a newly appearing light gray still has dark and clear
    // actions. This test is not evidence that optical overshoot was removed.
    int dark=0,clear=0;
    for(int f=0; f<reader->phases; ++f) {
        dark+=action(reader,f,12,15)==1;
        clear+=action(reader,f,12,15)==2;
    }
    assert(dark==(RICKYOS_PLUS_READER_SHORT_WAVEFORM ? 13 : 18) && clear==7);
    assert(fwrite(reader->luts,1,(size_t)reader->phases*16*4,stdout)==EXPECTED_READER_PHASES*16*4);
}
'''

def expected_reader(short):
    source = (DISPLAY / 'waveforms/my_waveform_gl.h').read_text()
    source = source[source.index('= {') + 3:source.index('};')]
    raw = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', source))
    assert len(raw) == 48 * 16 * 4
    if not short:
        expected = bytearray(raw)
        for f in range(48):
            for gray in range(16):
                expected[(f * 16 + gray) * 4 + gray // 4] &= ~(3 << (6 - 2 * (gray % 4)))
        return bytes(expected)
    sequences = {}
    for to in range(16):
        for old in range(16):
            seq = [(raw[(f * 16 + to) * 4 + old // 4] >> (6 - 2 * (old % 4))) & 3 for f in range(48)]
            while seq and seq[0] == 0:
                seq.pop(0)
            while seq and seq[-1] == 0:
                seq.pop()
            erase, saturated = (1, 2) if to == 15 else (2, 1)
            erase_count = 0
            while erase_count < len(seq) and seq[erase_count] == erase:
                erase_count += 1
            rest = seq[erase_count:]
            saturation_count = 0
            while saturation_count < len(rest) and rest[saturation_count] == saturated:
                saturation_count += 1
            cut = min(0 if to == 15 else 5, max(0, saturation_count - 1))
            sequences[to, old] = ([erase] * min(11, erase_count)
                                  + [saturated] * (saturation_count - cut)
                                  + rest[saturation_count:])
    longest = max(map(len, sequences.values()))
    assert longest + 3 == 37
    expected = bytearray(37 * 16 * 4)
    for (to, old), seq in sequences.items():
        if to == old:
            continue
        for f, pulse in enumerate(seq, longest - len(seq)):
            expected[(f * 16 + to) * 4 + old // 4] |= pulse << (6 - 2 * (old % 4))
    return bytes(expected)

with tempfile.TemporaryDirectory(prefix='plus-reader-waveform-') as folder:
    work = Path(folder)
    (work / 'test.c').write_text(PROGRAM)
    (work / 'esp_log.h').write_text('#define ESP_LOGD(...) ((void)0)\n')
    for trimmed, phases in ((0, 48), (1, 37)):
        for short in (0, 1):
            binary = work / f'test-{trimmed}-{short}'
            subprocess.run([
                'clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', f'-DMY_WAVEFORM_TRIM={trimmed}',
                f'-DRICKYOS_PLUS_READER_SHORT_WAVEFORM={short}',
                f'-DEXPECTED_READER_PHASES={37 if short else 48}',
                f'-DEXPECTED_PHASES={phases}', '-I', str(work), '-I', str(DISPLAY),
                str(work / 'test.c'), str(DISPLAY / 'waveforms/my_waveform.c'),
                str(DISPLAY / 'waveforms/my_waveform_trim.c'), '-o', str(binary),
            ], check=True)
            actual = subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE).stdout
            assert actual == expected_reader(short), 'Reader pulses differ from independent expected model'
print('Default 48-phase reader and opt-in 37-phase experiment: all 256 transitions, original GC/GL, idempotence passed; optical acceptance pending')
