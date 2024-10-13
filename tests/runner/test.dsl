DefinitionBlock ("test.dsl", "DSDT", 2, "OEM", "MACHINE", 2) {
	Device (DEV) {
		OperationRegion (REG0, Pci_Config, 0, 0x100)
		Field (REG0) {
			FIE0, 64
		}

		Printf ("%o", FIE0)
	}
}
