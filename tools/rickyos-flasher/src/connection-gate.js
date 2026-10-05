// A missing or rejected release must not even open a real USB permission prompt.
// The demo has no SerialAdapter and remains available before firmware launch.
export function canConnect({ demo, release, supported, hashReady, confirmed, busy, connected }) {
  return !busy && !connected && confirmed &&
    (demo || Boolean(release && supported && hashReady));
}
