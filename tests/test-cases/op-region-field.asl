// Name: Operation Region fields
// Expect: int => 0

DefinitionBlock ("x.aml", "DSDT", 2, "uTEST", "TESTTABL", 0xF0F0F0F0)
{
    Method (MAIN, 0, NotSerialized)
    {
        OperationRegion (REG0, SystemMemory, 0, 0x1000)
        Field (REG0, AnyAcc, NoLock, Preserve) {
            FLD0, 3,
            FLD1, 7,
            Offset (8),
            FLD2, 31,
            FLD3, 1
        }

        FLD1 = 0
        FLD0 = 7
        If (FLD1 != 0) {
            Debug = "Modifying FLD0 modified FLD1"
            Return (One)
        }
        FLD1 = 0
        If (FLD0 != 7) {
            Debug = "Modifying FLD1 modified FLD0"
            Return (One)
        }

        FLD2 = 1 << 30
        If (FLD2 != (1 << 30)) {
            Debug = "Writing/Reading to FLD2 is broken"
            Return (One)
        }
        FLD3 = 1
        If (FLD2 != (1 << 30)) {
            Debug = "Modifying FLD3 modified FLD2"
            Return (One)
        }

        Return (Zero)
    }
}
