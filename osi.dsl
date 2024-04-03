DefinitionBlock ("osi.dsl", "DSDT", 2, "OEM", "MACHINE", 2) {
	Method (QOSI, 1) {
		Printf ("_OSI: %o", Arg0)

		If (Arg0 == "Windows 2001") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2001 SP1") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2001 SP2") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2001.1") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2006") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2009") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2012") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2013") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2015") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2016") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2017") {
			Return (Ones)
		}
		If (Arg0 == "Windows 2017.2") {
			Return (Ones)
		}
		Return (Zero)
	}
}
