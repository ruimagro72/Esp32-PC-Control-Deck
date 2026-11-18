# ESP32 PC Control Deck: server branch

The Java host that runs on the PC and talks to the Deck, the device with the 2.8 inch screen. The other parts are the
KryonOS fork (`es3c28p-support`, the firmware base), `deck` (the Deck app on the board) and `simulator` (a PC emulator).
Overview and branch plan: see the `main` branch, `docs/BRANCHES.md`.

Short version:

```bash
cd server
mvn -DskipTests package                                   # Java 21+, Maven 3.9+
mkdir -p target/plugins target/plugin-classes             # GPU plugins (the self-tests need them)
for dir in plugins/*/; do
  [ -d "${dir}src" ] || continue
  name=$(basename "$dir")
  tmp="target/plugin-classes/$name"
  rm -rf "$tmp" && mkdir -p "$tmp"
  javac --release 21 -nowarn -cp target/control-deck-host-0.7.0-all.jar -d "$tmp" $(find "${dir}src" -name "*.java")
  cp -r "${dir}src/META-INF" "$tmp/"
  jar --create --file "target/plugins/$name.jar" -C "$tmp" .
done
java -jar target/control-deck-host-*-all.jar --selftest   # self-tests, no hardware: Passed 101, failed 0, skipped 0
java -jar target/control-deck-host-*-all.jar              # run: API on 127.0.0.1:8080
```

Full guide (options, settings, BLE bridge, layout): [`server/README.md`](server/README.md).

- REST API and telemetry: [`docs/03-SERVER.md`](docs/03-SERVER.md)
- Protocol 4, the contract with the Deck app: [`docs/04-PROTOCOL.md`](docs/04-PROTOCOL.md)
- Security (tokens, who may call the API, firmware signing): [`docs/05-SECURITY.md`](docs/05-SECURITY.md)

Licence: no restrictions. This server code is the owner's own work: it is free to use, modify and sell, commercial or not,
provided as is, without warranty. No formal licence file is included. The third-party components it uses are listed in
`THIRD-PARTY.md`.
