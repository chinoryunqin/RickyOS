"""Bluetooth page turners: S31 BLE controller + IDF NimBLE host, central only, host in PSRAM."""
from pathlib import Path
import re
import struct
import json
root=Path(__file__).resolve().parents[1]
fw=root/'firmware/rickyos-plus-1.2'
defaults=dict(line.split('=',1) for line in (fw/'sdkconfig.defaults').read_text().splitlines()
              if line.startswith('CONFIG_') and '=' in line)
for key,value in {'CONFIG_BT_ENABLED':'y','CONFIG_BT_NIMBLE_ENABLED':'y','CONFIG_BTDM_CTRL_MODE_BLE_ONLY':'y',
                  'CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL':'y','CONFIG_BT_NIMBLE_ROLE_CENTRAL':'y',
                  'CONFIG_BT_NIMBLE_ROLE_OBSERVER':'y','CONFIG_BT_NIMBLE_MAX_CONNECTIONS':'1',
                  'CONFIG_BT_NIMBLE_NVS_PERSIST':'y'}.items():
 assert defaults.get(key)==value,(key,defaults.get(key))
for key in ('CONFIG_SPIRAM_BOOT_HW_INIT','CONFIG_SPIRAM_BOOT_INIT','CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY'):
 assert defaults.get(key)=='y',f'External BLE BSS requires early PSRAM: {key}'
assert defaults['CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL']=='65536','Do not spend the internal DMA reserve on BLE'
text=(fw/'sdkconfig.defaults').read_text()
for role in ('PERIPHERAL','BROADCASTER'):
 assert f'# CONFIG_BT_NIMBLE_ROLE_{role} is not set' in text
assert 'CONFIG_BT_BLUEDROID_ENABLED=y' not in text

cmake=(fw/'components/rickyos_application/CMakeLists.txt').read_text()
gate=cmake[cmake.index('if(CONFIG_BT_NIMBLE_ENABLED)\n  # Host memory'):]
gate=gate[:gate.index('endif()')]
for flag in ('FREEINK_CAP_BLE_HID_HOST=1','CROSSPOINT_BLE_HOST_PSRAM=1','CONFIG_NIMBLE_CPP_IDF=1',
             'CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED=1','CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED=1'):
 assert flag in gate,flag
assert ' bt\n' in cmake[cmake.index(' REQUIRES '):][:120]

# The CMake passkey edit, applied to the pinned SDK source: one callback removed, none left.
host=(fw/'upstream/freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp').read_text()
pattern=re.search(r'string\(REGEX MATCH "(.*?)"\n',cmake).group(1)
pattern=pattern.replace('\\\\','\\').replace('\\n','\n')
matches=re.findall(pattern,host)
assert len(matches)==1,matches
edited=host.replace(matches[0],'',1)
assert 'onPassKeyDisplay(' not in edited and 'void onPassKeyEntry(' in edited and edited.count('{')==edited.count('}')

port=(fw/'components/rickyos_application/port/PlusBluetooth.cpp').read_text()
assert 'bool bleInUse(void) { return true; }' in port and '#if !CONFIG_BT_NIMBLE_GAP_SERVICE' in port
shim=(fw/'components/rickyos_application/port/ble/nimble/esp_port/port/include/esp_nimble_mem.h').read_text()
assert '#include <esp_nimble_mem.h>' in shim

# Standby is pushed over the reader; the reader underneath must not keep BLE (and so the
# device) awake there.
manager=(fw/'upstream/src/activities/ActivityManager.cpp').read_text()
keep=manager[manager.index('bool ActivityManager::keepsBluetoothAlive() const {'):]
keep=keep[:keep.index('\n}\n')]
assert keep.index('if (standbyShowing()) return false;')<keep.index('std::any_of'),keep

build=root/'.local/build-rickyos-plus-1.2'
config=build/'config/sdkconfig.h'
if config.exists():
 c=config.read_text()
 assert '#define CONFIG_BT_NIMBLE_ENABLED 1' in c and '#define CONFIG_BTDM_CTRL_MODE_BLE_ONLY 1' in c
 assert 'CONFIG_BT_NIMBLE_ROLE_PERIPHERAL ' not in c and 'CONFIG_BT_BLUEDROID_ENABLED' not in c
 elf=(build/'RickyOS-Plus-1.2.elf').read_bytes()
 assert b'BLE HID host started' in elf and b'ble-conn' in elf,'Linked application is missing the BLE HID host'
 assert '#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1' in c
 assert '#define CONFIG_SPIRAM_BOOT_INIT 1' in c and '#define CONFIG_SPIRAM_BOOT_HW_INIT 1' in c
 # Read the real ELF32 symbol table without requiring pyelftools or a toolchain
 # on host Python's PATH. A config flag alone does not prove actual placement.
 assert elf[:5]==b'\x7fELF\x01' and elf[5]==1
 shoff=struct.unpack_from('<I',elf,32)[0]
 shsize,count=struct.unpack_from('<HH',elf,46)
 sections=[struct.unpack_from('<10I',elf,shoff+i*shsize) for i in range(count)]
 symbols={}
 for section in sections:
  if section[1]!=2:continue
  strings=sections[section[6]];names=elf[strings[4]:strings[4]+strings[5]]
  for at in range(section[4],section[4]+section[5],section[9]):
   name,value,size,info,other,index=struct.unpack_from('<IIIBBH',elf,at)
   if name and index:symbols[names[name:names.index(0,name)].decode()]=value
 for symbol in ('dns_table','_bt_bss_start'):
  assert 0x50000000<=symbols[symbol]<0x51000000,(symbol,hex(symbols[symbol]))
 # This IDF links host globals in a separate libnimble archive. The generic
 # libbt scheme does not move those; do not claim all BLE BSS was relocated.
 assert 0x2f000000<=symbols['ble_hs_state_ctx']<0x30000000
 # Vendor controller ISR state remains internal, as required by IDF's mapping.
 assert 0x2f000000<=symbols['_bt_controller_bss_start']<0x30000000
 commands=json.loads((build/'compile_commands.json').read_text())
 diagnostic=any('-DRICKYOS_PLUS_BLE_DIAG=1' in row['command'] for row in commands)
 for command in (b'BLE_DIAG_OPEN',b'BLE_DIAG_START',b'BLE_DIAG_HOME'):
  assert (command in elf)==diagnostic,'Diagnostic commands do not match the configured build'
 if not diagnostic:
  assert b'Starting after idle cancellation' not in elf and b'[BleHid] report len=' not in elf
 print('Plus Bluetooth: config, CMake gate, passkey edit and linked BLE host verified')
else:
 print('Plus Bluetooth: config, CMake gate and passkey edit verified (no build to inspect)')
