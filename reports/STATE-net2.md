# STATE scx86-gap-net

- Esito parziale, fail-closed; non è multiplayer LAN end-to-end.
- Storm privata: `build/qa/gap-net/storm.orig.private.dll`, SHA-256 uguale all’originale; disassembly `storm-snet-disasm.txt`.
- Storm 0x1503E320 prova `SnpBind` stdcall 2 argomenti, tabella size min 0x50/max 0x70 e `spiInitialize` +0x1c a 5 argomenti; 0x1503AE50 chiama `+0x50` con 4 argomenti.
- Mappa `NetFunctions` completa nel report; call-site e label non provati per la maggior parte degli slot. `SnpGame/GAMELIST/SNETADDR`, SelectGame e ExternalMessage restano UNKNOWN.
- Nessuna modifica sorgente: non inventare ABI, payload o discovery. GameList vuota; SelectGame/ExternalMessage non implementati.
- Gate preesistente copiato in `build/qa/gap-net/x86_snp_lan_test`: PASS per UDP loopback `spiSend/Receive/Free`; nessuna prova di discovery/UI/game.
- Nessuna istanza avviata; `run-isolated.sh list` mostrava tutte quelle registrate non attive.
- Analisi completa, limiti e riferimenti: `reports/REPORT-net2.md`.
