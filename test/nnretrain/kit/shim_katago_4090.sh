#!/bin/bash
# PATH shim: runs the harness's `katago gtp ...` opponent on the 4090 over ssh.
# cwd = katago user's home (writable for gtp_logs); models/config absolute.
OVR=""; prev=""
for a in "$@"; do
  [ "$prev" = "-override-config" ] && OVR="$a"
  prev="$a"
done
export SSH_ASKPASS=/tmp/pw4090.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0
exec ssh -T katago@10.0.0.17 "cd /d C:\Users\katago && C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64\katago.exe gtp -model C:\Users\crook\LizzieYZY\katago-networks\kata9x9-b18c384nbt-20231025.bin.gz -human-model C:\Users\crook\LizzieYZY\katago-networks\b18c384nbt-humanv0.bin.gz -config C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64\default_gtp.cfg -override-config $OVR"
