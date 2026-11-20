# ISPF Member Statistics

How `zo` handles and validates ISPF member statistics stored inside PDS directory entries.

## Storage in PDS Directories

PDS directory blocks (256 bytes) contain variable-length member entries. Each entry starts with a 12-byte prefix:

- `NAME` (8 bytes)
- `TTR` (3 bytes)
- `C` (1 byte, control/flags)

The rest of the entry contains up to 62 bytes of custom `USER_DATA`. The length is determined by bits 3-7 of the control byte `C` (in halfwords).

ISPF uses this `USER_DATA` field to store member statistics:

- **Standard stats:** 30 bytes (15 halfwords).
- **Extended stats:** 40 bytes (20 halfwords), used if member line counts exceed 65,535.

## Validation Logic (`zds.cpp`)

Because directory `USER_DATA` can store anything (such as load module linkage editor data or custom SCM metadata), `zo` verifies if the bytes actually contain valid ISPF stats before parsing them. This prevents displaying garbled text or encountering errors.

We run two checks in `is_valid_ispf_stats`:

1. **Size & Flag Check:** Standard stats must be exactly 30 bytes, and the `extended` field must end with blanks (`"  "`). Extended stats must be exactly 40 bytes, and the extended flag (`0x20` in `flags`) must be active.
2. **Century Check:** The creation and modification century bytes must be either `0x00` (1900s) or `0x01` (2000s).

If these checks fail, we safely leave the member attributes blank.

## Manual Testing

To verify this:

1. **Find a corrupted member:** Find or create a PDS member with non-ISPF directory user data (e.g., load module entries or custom SCM tags).
2. **Check ISPF:** Open the data set in **ISPF Option 3.4**. The corrupted member should show blanks, warnings, or question marks (`??/??/??`).
3. **Check zo:** List member attributes for the data set via the `zo` CLI:
   ```bash
   zo ds list-members "MY.CORRUPTED.PDS" --attributes
   ```
   Alternatively you can check the member attributes using an SSH profile in Zowe Explorer.
4. **Verify Result:** The member(s) should list successfully without crashing and the corrupted attributes should simply be blank.
