// Let NSS trust roots from the Windows certificate store.
// Discord's gateway/status endpoints can otherwise fail with
// SEC_ERROR_UNKNOWN_ISSUER when the bundled nssckbi root set is stale.
pref("security.enterprise_roots.enabled", true);
