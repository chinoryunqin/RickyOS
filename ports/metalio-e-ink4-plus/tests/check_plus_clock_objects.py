import json,shlex,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
commands=json.loads((root / '.local/build-rickyos-plus-1.2/compile_commands.json').read_text())
for filename in ['TrustedTime.cpp','WifiSelectionActivity.cpp']:
 entry=next(x for x in commands if x['file'].endswith('/'+filename)); args=shlex.split(entry['command']); compiler=Path(args[0]); nm=compiler.with_name(compiler.name.replace('g++','nm'))
 obj=Path(entry['directory'])/args[args.index('-o')+1]
 symbols=subprocess.check_output([str(nm),'-u','-C',str(obj)]).decode()
 if filename=='TrustedTime.cpp':
  assert 'HalClock::requestSync()' in symbols and 'HalClock::syncNow(' in symbols, symbols
  for forbidden in ['configTzTime','sntp_set_time_sync_notification_cb','sntp_get_sync_status']:
   assert forbidden not in symbols, forbidden
 else:
  assert 'HalClock::requestSync()' in symbols, symbols
  assert 'HalClock::syncNow(' not in symbols, symbols
 print(filename+': linked object uses the Plus clock owner; legacy SNTP calls absent')
