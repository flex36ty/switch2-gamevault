# Changelog

## 0.4.5 — 2026-11-18

### Fixed
- Mario Kart World's installed update was rejected as "Unsupported update metadata".
  The installed NCM record is 272 bytes; the previous validator incorrectly expected
  3,412 bytes using the packaged update's extended-data size field.
- Backup format schema 2 preserves the complete, bounded installed NCM record,
  including its opaque trailing bytes, rather than reconstructing its size.
- Import and restore share the updated layout validation and retain schema-1 support.

### Validation
- Independently cross-check archive entries with NCM ListContentInfo before copying.
- Re-read and compare the complete NCM record before publishing a completed backup.
- Keep existing title, content count/type/ID, size, checksum and recovery checks.
- Add regression coverage for the observed MKW lengths, legacy layouts, metadata
  preservation, wrong identities, unsupported dependencies and malformed bounds.
- Native build and regression tests passed; uploaded NRO readback verified.
- User confirmed Mario Kart World backup now starts. Full backup completion and
  restoring that backup have not yet been verified.

### Compatibility
- Use Game Vault 0.4.5 or later to restore backups created by 0.4.5.
- Older schema-1 backups remain supported. No HEN update or restart is required
  when updating the app on an already-running supported HEN setup.
- Working kaiHEN fsipc-v1 is unchanged. Previous releases remain available.

## 0.4.4 — diagnostic build (not a published release)
- Added metadata header traces to isolate the installed update size mismatch.

## 0.4.3
- Direct cartridge control-metadata fallback for game title and icon lookup.
- Cartridge-first Backup tab and original cartridge base archive backup path.
- Graphite UI, independent game/update/save options, SD/FTP/SMB storage,
  main-thread USB MTP, persistent FTP and Switch-hosted web interface.
- Published source, working HEN runtime package, credits and console screenshots.
