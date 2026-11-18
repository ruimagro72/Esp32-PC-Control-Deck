# Third-party components and licences (server branch)

The server code is the owner's own code, with no restrictions. This table covers only the third-party components used by
this branch. "To confirm" means the licence is not stated in the repository and has not been checked against the upstream
source.

| Component | Where in this branch | Licence | Source and notes |
|---|---|---|---|
| Jackson `jackson-databind` 2.17.2 | `server/pom.xml` (dependency, shaded into the jar) | To confirm | Apache-2.0 expected; check upstream. |
| SQLite JDBC `sqlite-jdbc` 3.46.1.3 | `server/pom.xml` (dependency, shaded into the jar) | To confirm | Apache-2.0 expected; check upstream. |
| jSerialComm 2.11.0 | `server/pom.xml` (dependency, shaded into the jar) | To confirm | Used for the USB serial bridge. Check upstream. |
| Maven plugins (compiler 3.13.0, jar 3.4.1, shade 3.6.0) | `server/pom.xml` (build only, not in the jar) | To confirm | Build tools, not redistributed. |
| libsystemd (sd-bus), used by the BLE bridge | `server/ble-bridge/` | LGPL-2.1 (system library) | Linked dynamically from the system. Not redistributed. |
