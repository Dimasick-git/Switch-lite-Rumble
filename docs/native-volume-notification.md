# Native volume HUD path (alpha19)

The alpha18 console log shows min/max requests (audctl 2/3) on button presses,
but not GetTargetVolume (0). The actual per-game gain changes successfully.

Local analysis of the installed audio program (0100000000000014) identifies an
`ovln:snd` sender with SourceName `overlay`. Its notification builder at text
offset 0x29050 emits tag 1004 with data length 0x50. The first byte contains the
volume, the second a flag; the third maps audio targets (Speaker -> 1,
Headphones -> 0, TV -> 2, USB -> 3, Bluetooth -> 4). The system notification is
separate from audctl's GetTargetVolume reply. Command 10200 merely zero-fills a
0x100-byte response in this build; it is not a shared-memory volume interface.

Alpha19 proxies ovln:rcv for system applets. Its receiver explicitly forwards
AddSource, RemoveSource, GetReceiveEventHandle, Receive, and ReceiveWithTick.
Only a receiver subscribed to `overlay` can modify a packet, and only tag 1004,
size 0x50, mapped Speaker, volume 0..15, and a valid flag are accepted. Only its
volume byte changes. Outside active speaker/game mode the native message stays
unchanged. The native receive event and ReceiveWithTick timestamp are retained.

The receiver is a regular SF interface implementing every known command; it
does not request implicit MITM forwarding from a regular session (alpha15's bug).
Native domain IDs are associated with the returned object using the existing
Atmosphere output-object mechanism. No firmware image is modified or included
in the release; private local analysis files are excluded from packaging.

ABI reference: https://switchbrew.org/wiki/PSC_services#ovln:rcv
RawMessage is 0x80 bytes. ReceiveWithTick output is 0x88 bytes, with RawMessage
first according to SF's ascending-alignment ordering. Compile-time assertions
check this layout; host tests check every volume step, byte preservation, bad
packet rejection, and timestamp preservation. Console HUD validation is pending.
