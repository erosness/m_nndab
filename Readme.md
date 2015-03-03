# nndab

Talk to the Venice9 DAB module using nanomsg. Start the process and do this:

    printf "\x00\x01\x02\x01\x00\x00" | nanocat --req --connect tcp://192.168.0.222:12000 -F - -Q 

That should give you the binary blob response that the DAB says. Use
the dab chicken-egg to decode/encode this binary data.
