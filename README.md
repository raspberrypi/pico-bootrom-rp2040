This is the B0 version of the RP2040 bootrom.

The version on the chip was built in _Release_ mode using an _arm-cortex_m0-eabi_ version of
[crosstool-NG](https://crosstool-ng.github.io/crostool-NG) with GCC version 7.2.0.

Note the GIT revision info (included in the bootrom) on chip does not match the GIT revision of this
branch. Additionally, certain `DMB` instructions are encoded as `DMB SY` vs `DMB ISH` when building
with current SDKs although this has no functional effect on the RP2040.
