# Existing platform regeneration only; does not connect to or program a board.
set workspace [file normalize [file join [file dirname [info script]] ../../..]]
setws $workspace
platform active BFSK_Tx
platform generate
puts {BFSK_PLATFORM_REGENERATED}
exit
