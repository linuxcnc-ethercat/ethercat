# Application re-initialization after a slave returns

Some slaves lose application-written SDO or SII settings when power-cycled.
The master restores its own sync-manager, FMMU, PDO and DC configuration when
such a slave returns, but it cannot know about settings written directly by
the application. The optional `ReinitHold` feature keeps the slave in PREOP
until the application has restored those settings.

Slaves without this feature keep the existing behavior.

## Enable the feature before activation

Enable the hold for each slave configuration that needs it and confirm the
initial application configuration before activating the master:

```c
ec_slave_config_t *sc;

sc = ecrt_master_slave_config(master, alias, position, vendor, product);
if (!sc)
    return -1;

if (ecrt_slave_config_flag(sc, "ReinitHold", 1) ||
        ecrt_slave_config_flag(sc, "ReinitHoldTimeoutMs", 30000))
    return -1;

/* Perform the same initial SDO/SII configuration used after a return. */
if (restore_volatile_configuration(sc) < 0)
    return -1;

/* Do not hold the first configuration performed during activation. */
if (ecrt_slave_config_reinit_done(sc) < 0)
    return -1;

if (ecrt_master_activate(master))
    return -1;
```

Omitting the initial confirmation intentionally makes the first configuration
stop in PREOP as well.

## Handle a returning slave

Poll `ecrt_slave_config_state()` from an application management thread. When
`reinit_pending` is set, restore every volatile setting and confirm success:

```c
void check_slave_reinit(ec_slave_config_t *sc)
{
    ec_slave_config_state_t state;

    if (ecrt_slave_config_state(sc, &state) < 0)
        return;

    if (!state.reinit_pending && !state.reinit_timeout)
        return;

    /* Blocking mailbox operations belong in a non-realtime thread. */
    if (restore_volatile_configuration(sc) < 0)
        return; /* Retry later; do not release the hold. */

    if (ecrt_slave_config_reinit_done(sc) < 0)
        return;
}
```

`restore_volatile_configuration()` is application-specific. It can use SDO
requests or downloads and `ecrt_master_sii_write()` to repeat writes made
during initialization. It must be idempotent and must not change the process
image registered before activation. Mailbox requests are served while the
slave is held in PREOP, so these operations can complete before confirmation.

The real-time cyclic task should continue calling `ecrt_master_receive()`,
domain processing and `ecrt_master_send()`; it must not block on SDO/SII
transactions. Other slaves continue running. The held slave does not answer
its process-data datagrams until it reaches SAFEOP again.

After confirmation, the master resumes with its configured SDOs, PDO setup,
DC setup and the SAFEOP/OP transitions. Do not call the confirmation function
if restoration failed.

## Timeout and retry

`ReinitHoldTimeoutMs` defaults to 30000 ms. On expiry the master sets
`reinit_timeout` and the slave error flag and leaves the slave in PREOP. It
never advances an unconfirmed slave on its own.

A later successful restoration may call `ecrt_slave_config_reinit_done()`.
This clears the timeout error and starts a full configuration, which holds in
PREOP once more. The application then restores and confirms a second time;
this deliberate repetition is why the restoration routine must be idempotent.

Use `EC_HAVE_REINIT_HOLD` at compile time to check for the API and the two
state bits. Use `EC_HAVE_SII_ACCESS` to check for the runtime SII read/write
API.
