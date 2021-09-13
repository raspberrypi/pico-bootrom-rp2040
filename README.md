This is the B2 version of the RP2040 bootrom.

The version on the chip was built in _Debug_ mode using GCC 9.3.1 (GNU Arm Embedded Toolchain 9-2020-q2-update).

Note the GIT revision info (included in the bootrom) on chip does not match the GIT revision of this
branch. Additionally, certain `DMB` instructions are encoded as `DMB SY` vs `DMB ISH` when building
with current SDKs although this has no functional effect on the RP2040.
