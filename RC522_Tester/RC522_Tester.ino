#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>

#define SS_PIN     10
#define RST_PIN    9
#define BUZZER_PIN 4
#define BUZZER_MS  1500

const char* AP_SSID = "RC522-Tester";
const char* AP_PASS = "12345678";

MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);
Preferences prefs;

MFRC522::MIFARE_Key keyA;
MFRC522::MIFARE_Key keyB;

String lastUID = "";
String lastMessage = "";
int buzzerDurationMs = BUZZER_MS;

const byte COMMON_KEYS[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}
};
const int COMMON_KEYS_COUNT = 8;

void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

String uidToString(MFRC522::Uid *uid) {
  String s = "";
  for (byte i = 0; i < uid->size; i++) {
    if (uid->uidByte[i] < 0x10) s += "0";
    s += String(uid->uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

bool parseHexKey(String hex, byte *out) {
  if (hex.length() != 12) return false;
  for (int i = 0; i < 6; i++) {
    String byteStr = hex.substring(i * 2, i * 2 + 2);
    out[i] = strtol(byteStr.c_str(), NULL, 16);
  }
  return true;
}

bool parseHexBlock(String hex, byte *out) {
  if (hex.length() != 32) return false;
  for (int i = 0; i < 16; i++) {
    String byteStr = hex.substring(i * 2, i * 2 + 2);
    out[i] = strtol(byteStr.c_str(), NULL, 16);
  }
  return true;
}

String bytesToHex(byte *buf, int len) {
  String s = "";
  for (int i = 0; i < len; i++) {
    if (buf[i] < 0x10) s += "0";
    s += String(buf[i], HEX);
  }
  s.toUpperCase();
  return s;
}

bool waitCard(int timeoutMs = 3000) {
  unsigned long start = millis();
  while (millis() - start < (unsigned long)timeoutMs) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) return true;
    delay(50);
  }
  return false;
}

void haltCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

const char* PAGE_HTML PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>RC522 Tester</title>
<style>
body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:0;padding:16px}
h1{font-size:20px;color:#0f0}
.card{background:#1c1c1c;border-radius:10px;padding:14px;margin-bottom:14px}
h2{font-size:15px;color:#0af;margin-top:0}
input,select{width:100%;padding:8px;margin:5px 0;background:#222;color:#eee;border:1px solid #444;border-radius:6px;box-sizing:border-box}
button{width:100%;padding:10px;margin:5px 0;background:#0a7;border:none;color:#fff;border-radius:6px;font-size:14px}
button.red{background:#a22}
button.blue{background:#25a}
#log{background:#000;color:#0f0;padding:10px;border-radius:6px;font-size:12px;white-space:pre-wrap;height:200px;overflow-y:auto}
.row{display:flex;gap:6px}
.row>div{flex:1}
</style>
</head>
<body>
<h1>RC522 Card Tester</h1>

<div class="card">
<h2>Baca Kartu</h2>
<button onclick="readUID()">Baca UID</button>
<button onclick="readAll()">Baca Semua Sektor (Key Default)</button>
<button class="blue" id="autoReadBtn" onclick="toggleAutoRead()">Mulai Mode Otomatis (Baca UID)</button>
</div>

<div class="card">
<h2>Tulis Blok</h2>
<input id="wBlock" placeholder="Nomor Block (1-14, hindari block 0)">
<input id="wData" placeholder="Data Hex 32 karakter (16 byte)">
<input id="wKey" placeholder="Key A/B Hex 12 karakter" value="FFFFFFFFFFFF">
<select id="wKeyType"><option value="A">Key A</option><option value="B">Key B</option></select>
<button onclick="writeBlock()">Tulis</button>
</div>

<div class="card">
<h2>Ganti Key Sektor</h2>
<input id="cSector" placeholder="Nomor Sektor (0-15)">
<input id="cOldKey" placeholder="Key A Lama Hex" value="FFFFFFFFFFFF">
<input id="cNewKeyA" placeholder="Key A Baru Hex (12 char)">
<input id="cNewKeyB" placeholder="Key B Baru Hex (12 char)">
<button class="red" onclick="changeKey()">Ganti Key</button>
</div>

<div class="card">
<h2>Simpan Kartu (Flash)</h2>
<input id="sName" placeholder="Nama Kartu">
<button onclick="saveCard()">Simpan Kartu Yang Terbaca Terakhir</button>
<button class="blue" onclick="listCards()">Lihat Kartu Tersimpan</button>
<button class="red" onclick="clearCards()">Hapus Semua Kartu Tersimpan</button>
</div>

<div class="card">
<h2>Test Kartu</h2>
<p>Tempel kartu lalu tekan tombol. Buzzer bunyi jika cocok dengan kartu tersimpan.</p>
<button onclick="testCard()">Test Sekarang</button>
<button class="blue" id="autoTestBtn" onclick="toggleAutoTest()">Mulai Mode Otomatis (Test Kartu)</button>
<input id="buzzMs" placeholder="Durasi Buzzer (ms)" value="1500">
<button class="blue" onclick="setBuzz()">Set Durasi Buzzer</button>
</div>

<div class="card">
<h2>Baca Dengan Key Custom</h2>
<input id="rKey" placeholder="Key Hex 12 karakter" value="FFFFFFFFFFFF">
<select id="rKeyType"><option value="A">Key A</option><option value="B">Key B</option></select>
<button onclick="readAllCustom()">Baca Semua Sektor (Key Custom)</button>
</div>

<div class="card">
<h2>Cek / Brute Key Sektor</h2>
<p>Coba beberapa key default umum di tiap sektor buat cari key yang aktif.</p>
<button onclick="bruteKeys()">Cek Key Semua Sektor</button>
</div>

<div class="card">
<h2>Access Bits Sektor</h2>
<input id="abSector" placeholder="Nomor Sektor (0-15)">
<input id="abKey" placeholder="Key A Hex" value="FFFFFFFFFFFF">
<input id="abBits" placeholder="Access Bits Hex 6 karakter (contoh FF0780)" value="FF0780">
<button class="red" onclick="setAccessBits()">Set Access Bits</button>
</div>

<div class="card">
<h2>Value Block</h2>
<input id="vBlock" placeholder="Nomor Block">
<input id="vKey" placeholder="Key Hex" value="FFFFFFFFFFFF">
<select id="vKeyType"><option value="A">Key A</option><option value="B">Key B</option></select>
<button onclick="valueInit()">Jadikan Value Block (init 0)</button>
<input id="vAmount" placeholder="Jumlah">
<div class="row">
<div><button onclick="valueOp('increment')">Increment</button></div>
<div><button onclick="valueOp('decrement')">Decrement</button></div>
</div>
<button class="blue" onclick="valueRead()">Baca Value</button>
</div>

<div class="card">
<h2>Backup &amp; Clone Kartu</h2>
<input id="bkName" placeholder="Nama Backup">
<input id="bkKey" placeholder="Key Hex" value="FFFFFFFFFFFF">
<button onclick="backupCard()">Backup Kartu Ke Flash</button>
<button class="blue" onclick="listBackups()">Lihat Backup Tersimpan</button>
<input id="cloneName" placeholder="Nama Backup Yang Mau Diclone">
<button class="red" onclick="cloneCard()">Clone Ke Kartu Baru (Magic Card)</button>
</div>

<div class="card">
<h2>Tulis Block 0 (Magic Card / UID Changeable)</h2>
<input id="uidNew" placeholder="UID Baru Hex (8 karakter, 4 byte)">
<button class="red" onclick="writeUID()">Tulis UID Baru</button>
</div>

<div class="card">
<h2>Format Kartu</h2>
<p>Kembalikan semua sektor ke key default FFFFFFFFFFFF dan access bits default.</p>
<input id="fmtKey" placeholder="Key A Sekarang Hex" value="FFFFFFFFFFFF">
<button class="red" onclick="formatCard()">Format Ke Default</button>
</div>

<div class="card">
<h2>NFC Otomatis (NDEF)</h2>
<select id="ndefType" onchange="ndefTypeChange()">
<option value="url">Link / URL</option>
<option value="text">Teks Biasa</option>
<option value="tel">Nomor Telepon</option>
<option value="sms">SMS</option>
<option value="email">Email</option>
<option value="wifi">WiFi</option>
<option value="vcard">Kontak (vCard)</option>
</select>
<div id="ndefFields"></div>
<input id="ndefAAR" placeholder="Package Android (opsional, contoh com.whatsapp)">
<button onclick="writeNDEF()">Tulis ke Kartu</button>
<p>Setelah ini tempel kartu ke belakang HP yang NFC-nya aktif (bukan lewat web ini).</p>
</div>

<div class="card">
<h2>Info Alat</h2>
<button class="blue" onclick="selfTest()">Cek Modul RC522 (Self Test)</button>
</div>

<div class="card">
<h2>Kelola Kartu Tersimpan</h2>
<input id="delUid" placeholder="UID Yang Mau Dihapus">
<button class="red" onclick="deleteCard()">Hapus Kartu Ini Saja</button>
</div>

<div class="card">
<h2>Log</h2>
<div id="log">siap.</div>
</div>

<script>
function log(t){document.getElementById('log').innerText=t}
async function call(url,opts){
  try{
    let r=await fetch(url,opts);
    let t=await r.text();
    log(t);
  }catch(e){log("Error: "+e)}
}
function readUID(){call('/read_uid')}
function readAll(){call('/read_all')}

let autoReadTimer=null;
function toggleAutoRead(){
  let btn=document.getElementById('autoReadBtn');
  if(autoReadTimer){
    clearInterval(autoReadTimer);autoReadTimer=null;
    btn.innerText='Mulai Mode Otomatis (Baca UID)';
    btn.className='blue';
  }else{
    btn.innerText='Berhenti Mode Otomatis';
    btn.className='red';
    autoReadTimer=setInterval(async()=>{
      try{let r=await fetch('/poll_uid');let t=await r.text();if(t!=='-')log(t)}catch(e){}
    },500);
  }
}

let autoTestTimer=null;
function toggleAutoTest(){
  let btn=document.getElementById('autoTestBtn');
  if(autoTestTimer){
    clearInterval(autoTestTimer);autoTestTimer=null;
    btn.innerText='Mulai Mode Otomatis (Test Kartu)';
    btn.className='blue';
  }else{
    btn.innerText='Berhenti Mode Otomatis';
    btn.className='red';
    autoTestTimer=setInterval(async()=>{
      try{let r=await fetch('/poll_test');let t=await r.text();if(t!=='-')log(t)}catch(e){}
    },600);
  }
}
function writeBlock(){
  let b=document.getElementById('wBlock').value;
  let d=document.getElementById('wData').value;
  let k=document.getElementById('wKey').value;
  let kt=document.getElementById('wKeyType').value;
  call('/write_block?block='+b+'&data='+d+'&key='+k+'&keytype='+kt)
}
function changeKey(){
  let s=document.getElementById('cSector').value;
  let ok=document.getElementById('cOldKey').value;
  let nka=document.getElementById('cNewKeyA').value;
  let nkb=document.getElementById('cNewKeyB').value;
  call('/change_key?sector='+s+'&oldkey='+ok+'&newkeya='+nka+'&newkeyb='+nkb)
}
function saveCard(){
  let n=document.getElementById('sName').value;
  call('/save_card?name='+encodeURIComponent(n))
}
function listCards(){call('/list_cards')}
function clearCards(){call('/clear_cards')}
function testCard(){call('/test_card')}
function setBuzz(){call('/set_buzzer?ms='+document.getElementById('buzzMs').value)}
function readAllCustom(){
  let k=document.getElementById('rKey').value;
  let kt=document.getElementById('rKeyType').value;
  call('/read_all_custom?key='+k+'&keytype='+kt)
}
function bruteKeys(){call('/brute_keys')}
function setAccessBits(){
  let s=document.getElementById('abSector').value;
  let k=document.getElementById('abKey').value;
  let b=document.getElementById('abBits').value;
  call('/set_access?sector='+s+'&key='+k+'&bits='+b)
}
function valueInit(){
  let b=document.getElementById('vBlock').value;
  let k=document.getElementById('vKey').value;
  let kt=document.getElementById('vKeyType').value;
  call('/value_init?block='+b+'&key='+k+'&keytype='+kt)
}
function valueOp(op){
  let b=document.getElementById('vBlock').value;
  let k=document.getElementById('vKey').value;
  let kt=document.getElementById('vKeyType').value;
  let a=document.getElementById('vAmount').value;
  call('/value_op?block='+b+'&key='+k+'&keytype='+kt+'&op='+op+'&amount='+a)
}
function valueRead(){
  let b=document.getElementById('vBlock').value;
  let k=document.getElementById('vKey').value;
  let kt=document.getElementById('vKeyType').value;
  call('/value_read?block='+b+'&key='+k+'&keytype='+kt)
}
function backupCard(){
  let n=document.getElementById('bkName').value;
  let k=document.getElementById('bkKey').value;
  call('/backup_card?name='+encodeURIComponent(n)+'&key='+k)
}
function listBackups(){call('/list_backups')}
function cloneCard(){
  let n=document.getElementById('cloneName').value;
  call('/clone_card?name='+encodeURIComponent(n))
}
function writeUID(){
  let u=document.getElementById('uidNew').value;
  call('/write_uid?uid='+u)
}
function formatCard(){
  let k=document.getElementById('fmtKey').value;
  call('/format_card?key='+k)
}
function ndefTypeChange(){
  let t=document.getElementById('ndefType').value;
  let f=document.getElementById('ndefFields');
  if(t=='url') f.innerHTML='<input id="f1" placeholder="https://contoh.com">';
  else if(t=='text') f.innerHTML='<input id="f1" placeholder="Isi teks">';
  else if(t=='tel') f.innerHTML='<input id="f1" placeholder="Nomor telepon, contoh 081234567890">';
  else if(t=='sms') f.innerHTML='<input id="f1" placeholder="Nomor tujuan"><input id="f2" placeholder="Isi pesan">';
  else if(t=='email') f.innerHTML='<input id="f1" placeholder="Alamat email"><input id="f2" placeholder="Subjek"><input id="f3" placeholder="Isi pesan">';
  else if(t=='wifi') f.innerHTML='<input id="f1" placeholder="SSID"><input id="f2" placeholder="Password"><select id="f3"><option value="WPA">WPA/WPA2</option><option value="nopass">Tanpa Password</option></select>';
  else if(t=='vcard') f.innerHTML='<input id="f1" placeholder="Nama"><input id="f2" placeholder="Nomor telepon"><input id="f3" placeholder="Email (opsional)">';
}
ndefTypeChange();
function writeNDEF(){
  let t=document.getElementById('ndefType').value;
  let f1=document.getElementById('f1')?document.getElementById('f1').value:'';
  let f2=document.getElementById('f2')?document.getElementById('f2').value:'';
  let f3=document.getElementById('f3')?document.getElementById('f3').value:'';
  let aar=document.getElementById('ndefAAR').value;
  call('/write_ndef?type='+t+'&f1='+encodeURIComponent(f1)+'&f2='+encodeURIComponent(f2)+'&f3='+encodeURIComponent(f3)+'&aar='+encodeURIComponent(aar))
}
function selfTest(){call('/self_test')}
function deleteCard(){
  let u=document.getElementById('delUid').value;
  call('/delete_card?uid='+u)
}
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send(200, "text/html", PAGE_HTML);
}

const byte FALLBACK_KEYS[3][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}
};

bool authTrailerMultiKey(byte trailerBlock, byte *keyUsedOut) {
  for (int i = 0; i < 3; i++) {
    MFRC522::MIFARE_Key k;
    memcpy(k.keyByte, FALLBACK_KEYS[i], 6);
    MFRC522::StatusCode st = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
    if (st == MFRC522::STATUS_OK) {
      if (keyUsedOut) memcpy(keyUsedOut, FALLBACK_KEYS[i], 6);
      return true;
    }
    rfid.PCD_StopCrypto1();
  }
  return false;
}

void handlePollUID() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    lastUID = uidToString(&rfid.uid);
    String type = rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak));
    haltCard();
    server.send(200, "text/plain", "UID: " + lastUID + "\nTipe: " + type);
  } else {
    server.send(200, "text/plain", "-");
  }
}

void handlePollTest() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = uidToString(&rfid.uid);
    haltCard();
    lastUID = uid;
    prefs.begin("cards", true);
    int count = prefs.getInt("count", 0);
    String matchName = "";
    for (int i = 0; i < count; i++) {
      if (prefs.getString(("uid" + String(i)).c_str(), "") == uid) {
        matchName = prefs.getString(("name" + String(i)).c_str(), "");
        break;
      }
    }
    prefs.end();
    if (matchName != "") {
      beep(buzzerDurationMs);
      server.send(200, "text/plain", "COCOK: " + matchName + " (" + uid + ")");
    } else {
      server.send(200, "text/plain", "TIDAK COCOK. UID: " + uid);
    }
  } else {
    server.send(200, "text/plain", "-");
  }
}

void handleReadUID() {
  if (!waitCard()) {
    server.send(200, "text/plain", "Kartu tidak terdeteksi.");
    return;
  }
  lastUID = uidToString(&rfid.uid);
  String type = rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak));
  String out = "UID: " + lastUID + "\nTipe: " + type;
  haltCard();
  server.send(200, "text/plain", out);
}

void handleReadAll() {
  if (!waitCard()) {
    server.send(200, "text/plain", "Kartu tidak terdeteksi.");
    return;
  }
  lastUID = uidToString(&rfid.uid);
  String out = "UID: " + lastUID + "\n\n";
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = 18;

  for (byte sector = 0; sector < 16; sector++) {
    byte trailerBlock = sector * 4 + 3;
    status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &keyA, &(rfid.uid));
    if (status != MFRC522::STATUS_OK) {
      out += "Sektor " + String(sector) + ": gagal auth (key default salah)\n";
      continue;
    }
    for (byte b = 0; b < 4; b++) {
      byte block = sector * 4 + b;
      status = rfid.MIFARE_Read(block, buffer, &size);
      if (status == MFRC522::STATUS_OK) {
        out += "Blok " + String(block) + ": " + bytesToHex(buffer, 16) + "\n";
      } else {
        out += "Blok " + String(block) + ": gagal baca\n";
      }
    }
  }
  haltCard();
  server.send(200, "text/plain", out);
}

void handleWriteBlock() {
  if (!server.hasArg("block") || !server.hasArg("data") || !server.hasArg("key")) {
    server.send(200, "text/plain", "Parameter kurang.");
    return;
  }
  int block = server.arg("block").toInt();
  String dataHex = server.arg("data");
  String keyHex = server.arg("key");
  String keyType = server.arg("keytype");
  if (block == 0 || (block + 1) % 4 == 0) {
    server.send(200, "text/plain", "Blok tidak valid (jangan block 0 atau block trailer).");
    return;
  }
  byte data[16];
  byte key[6];
  if (!parseHexBlock(dataHex, data) || !parseHexKey(keyHex, key)) {
    server.send(200, "text/plain", "Format hex salah.");
    return;
  }
  if (!waitCard()) {
    server.send(200, "text/plain", "Kartu tidak terdeteksi.");
    return;
  }
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, key, 6);
  byte trailerBlock = (block / 4) * 4 + 3;
  MFRC522::PICC_Command cmd = (keyType == "B") ? MFRC522::PICC_CMD_MF_AUTH_KEY_B : MFRC522::PICC_CMD_MF_AUTH_KEY_A;
  MFRC522::StatusCode status = rfid.PCD_Authenticate(cmd, trailerBlock, &k, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    haltCard();
    server.send(200, "text/plain", "Auth gagal: " + String(rfid.GetStatusCodeName(status)));
    return;
  }
  status = rfid.MIFARE_Write(block, data, 16);
  haltCard();
  if (status != MFRC522::STATUS_OK) {
    server.send(200, "text/plain", "Tulis gagal: " + String(rfid.GetStatusCodeName(status)));
    return;
  }
  server.send(200, "text/plain", "Berhasil tulis blok " + String(block));
}

void handleChangeKey() {
  if (!server.hasArg("sector") || !server.hasArg("oldkey") || !server.hasArg("newkeya") || !server.hasArg("newkeyb")) {
    server.send(200, "text/plain", "Parameter kurang.");
    return;
  }
  int sector = server.arg("sector").toInt();
  String oldKeyHex = server.arg("oldkey");
  String newKeyAHex = server.arg("newkeya");
  String newKeyBHex = server.arg("newkeyb");
  byte oldKey[6], newKeyA[6], newKeyB[6];
  if (!parseHexKey(oldKeyHex, oldKey) || !parseHexKey(newKeyAHex, newKeyA) || !parseHexKey(newKeyBHex, newKeyB)) {
    server.send(200, "text/plain", "Format hex key salah.");
    return;
  }
  if (!waitCard()) {
    server.send(200, "text/plain", "Kartu tidak terdeteksi.");
    return;
  }
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, oldKey, 6);
  byte trailerBlock = sector * 4 + 3;
  MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    haltCard();
    server.send(200, "text/plain", "Auth gagal: " + String(rfid.GetStatusCodeName(status)));
    return;
  }
  byte trailerData[16];
  memcpy(trailerData, newKeyA, 6);
  trailerData[6] = 0xFF; trailerData[7] = 0x07; trailerData[8] = 0x80; trailerData[9] = 0x69;
  memcpy(trailerData + 10, newKeyB, 6);
  status = rfid.MIFARE_Write(trailerBlock, trailerData, 16);
  haltCard();
  if (status != MFRC522::STATUS_OK) {
    server.send(200, "text/plain", "Ganti key gagal: " + String(rfid.GetStatusCodeName(status)));
    return;
  }
  server.send(200, "text/plain", "Key sektor " + String(sector) + " berhasil diganti.");
}

void handleSaveCard() {
  if (lastUID == "") {
    server.send(200, "text/plain", "Belum ada kartu yang dibaca. Baca UID dulu.");
    return;
  }
  String name = server.hasArg("name") ? server.arg("name") : "";
  if (name == "") name = lastUID;

  prefs.begin("cards", false);
  int count = prefs.getInt("count", 0);
  bool exists = false;
  for (int i = 0; i < count; i++) {
    String key = "uid" + String(i);
    if (prefs.getString(key.c_str(), "") == lastUID) {
      exists = true;
      break;
    }
  }
  if (!exists) {
    prefs.putString(("uid" + String(count)).c_str(), lastUID);
    prefs.putString(("name" + String(count)).c_str(), name);
    prefs.putInt("count", count + 1);
  }
  prefs.end();
  server.send(200, "text/plain", "Kartu tersimpan: " + name + " (" + lastUID + ")");
}

void handleListCards() {
  prefs.begin("cards", true);
  int count = prefs.getInt("count", 0);
  String out = "Total kartu tersimpan: " + String(count) + "\n\n";
  for (int i = 0; i < count; i++) {
    String uid = prefs.getString(("uid" + String(i)).c_str(), "");
    String name = prefs.getString(("name" + String(i)).c_str(), "");
    out += String(i + 1) + ". " + name + " - " + uid + "\n";
  }
  prefs.end();
  server.send(200, "text/plain", out);
}

void handleClearCards() {
  prefs.begin("cards", false);
  prefs.clear();
  prefs.end();
  server.send(200, "text/plain", "Semua kartu tersimpan sudah dihapus.");
}

void handleSetBuzzer() {
  if (!server.hasArg("ms")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  buzzerDurationMs = server.arg("ms").toInt();
  server.send(200, "text/plain", "Durasi buzzer diset ke " + String(buzzerDurationMs) + " ms");
}

void handleReadAllCustom() {
  if (!server.hasArg("key")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  byte key[6];
  if (!parseHexKey(server.arg("key"), key)) { server.send(200, "text/plain", "Format key salah."); return; }
  String keyType = server.arg("keytype");
  MFRC522::PICC_Command cmd = (keyType == "B") ? MFRC522::PICC_CMD_MF_AUTH_KEY_B : MFRC522::PICC_CMD_MF_AUTH_KEY_A;
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  lastUID = uidToString(&rfid.uid);
  String out = "UID: " + lastUID + "\n\n";
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, key, 6);
  byte buffer[18]; byte size = 18;
  for (byte sector = 0; sector < 16; sector++) {
    byte trailerBlock = sector * 4 + 3;
    MFRC522::StatusCode status = rfid.PCD_Authenticate(cmd, trailerBlock, &k, &(rfid.uid));
    if (status != MFRC522::STATUS_OK) {
      out += "Sektor " + String(sector) + ": gagal auth\n";
      continue;
    }
    for (byte b = 0; b < 4; b++) {
      byte block = sector * 4 + b;
      status = rfid.MIFARE_Read(block, buffer, &size);
      out += "Blok " + String(block) + ": " + (status == MFRC522::STATUS_OK ? bytesToHex(buffer, 16) : "gagal baca") + "\n";
    }
  }
  haltCard();
  server.send(200, "text/plain", out);
}

void handleBruteKeys() {
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  lastUID = uidToString(&rfid.uid);
  String out = "UID: " + lastUID + "\n\n";
  for (byte sector = 0; sector < 16; sector++) {
    byte trailerBlock = sector * 4 + 3;
    bool found = false;
    for (int i = 0; i < COMMON_KEYS_COUNT && !found; i++) {
      MFRC522::MIFARE_Key k;
      memcpy(k.keyByte, COMMON_KEYS[i], 6);
      rfid.PICC_HaltA();
      MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
      if (status == MFRC522::STATUS_OK) {
        out += "Sektor " + String(sector) + ": Key A = " + bytesToHex((byte*)COMMON_KEYS[i], 6) + "\n";
        found = true;
      }
      rfid.PCD_StopCrypto1();
    }
    if (!found) out += "Sektor " + String(sector) + ": tidak ketemu di daftar key umum\n";
    if (!rfid.PICC_IsNewCardPresent()) rfid.PICC_ReadCardSerial();
  }
  haltCard();
  server.send(200, "text/plain", out);
}

void handleSetAccess() {
  if (!server.hasArg("sector") || !server.hasArg("key") || !server.hasArg("bits")) {
    server.send(200, "text/plain", "Parameter kurang."); return;
  }
  int sector = server.arg("sector").toInt();
  byte key[6];
  if (!parseHexKey(server.arg("key"), key)) { server.send(200, "text/plain", "Format key salah."); return; }
  String bitsHex = server.arg("bits");
  if (bitsHex.length() != 6) { server.send(200, "text/plain", "Access bits harus 6 karakter hex (3 byte)."); return; }
  byte accessBytes[3];
  for (int i = 0; i < 3; i++) accessBytes[i] = strtol(bitsHex.substring(i*2, i*2+2).c_str(), NULL, 16);

  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, key, 6);
  byte trailerBlock = sector * 4 + 3;
  MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) { haltCard(); server.send(200, "text/plain", "Auth gagal: " + String(rfid.GetStatusCodeName(status))); return; }

  byte currentTrailer[18]; byte size = 18;
  status = rfid.MIFARE_Read(trailerBlock, currentTrailer, &size);
  if (status != MFRC522::STATUS_OK) { haltCard(); server.send(200, "text/plain", "Baca trailer gagal."); return; }

  byte newTrailer[16];
  memcpy(newTrailer, currentTrailer, 6);
  newTrailer[6] = accessBytes[0];
  newTrailer[7] = accessBytes[1];
  newTrailer[8] = accessBytes[2];
  newTrailer[9] = 0x69;
  memcpy(newTrailer + 10, currentTrailer + 10, 6);
  status = rfid.MIFARE_Write(trailerBlock, newTrailer, 16);
  haltCard();
  if (status != MFRC522::STATUS_OK) { server.send(200, "text/plain", "Set access bits gagal: " + String(rfid.GetStatusCodeName(status))); return; }
  server.send(200, "text/plain", "Access bits sektor " + String(sector) + " berhasil diset.");
}

bool authForBlock(int block, byte *keyBytes, String keyType) {
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, keyBytes, 6);
  byte trailerBlock = (block / 4) * 4 + 3;
  MFRC522::PICC_Command cmd = (keyType == "B") ? MFRC522::PICC_CMD_MF_AUTH_KEY_B : MFRC522::PICC_CMD_MF_AUTH_KEY_A;
  return rfid.PCD_Authenticate(cmd, trailerBlock, &k, &(rfid.uid)) == MFRC522::STATUS_OK;
}

void handleValueInit() {
  if (!server.hasArg("block") || !server.hasArg("key")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  int block = server.arg("block").toInt();
  byte key[6];
  if (!parseHexKey(server.arg("key"), key)) { server.send(200, "text/plain", "Format key salah."); return; }
  String keyType = server.arg("keytype");
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  if (!authForBlock(block, key, keyType)) { haltCard(); server.send(200, "text/plain", "Auth gagal."); return; }
  byte valueBlockData[16] = {0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,(byte)block,(byte)(~block),(byte)block,(byte)(~block)};
  MFRC522::StatusCode st = rfid.MIFARE_Write(block, valueBlockData, 16);
  haltCard();
  if (st != MFRC522::STATUS_OK) { server.send(200, "text/plain", "Init value block gagal: " + String(rfid.GetStatusCodeName(st))); return; }
  server.send(200, "text/plain", "Blok " + String(block) + " dijadikan value block, nilai awal 0.");
}

void handleValueOp() {
  if (!server.hasArg("block") || !server.hasArg("key") || !server.hasArg("op") || !server.hasArg("amount")) {
    server.send(200, "text/plain", "Parameter kurang."); return;
  }
  int block = server.arg("block").toInt();
  byte key[6];
  if (!parseHexKey(server.arg("key"), key)) { server.send(200, "text/plain", "Format key salah."); return; }
  String keyType = server.arg("keytype");
  String op = server.arg("op");
  long amount = server.arg("amount").toInt();
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  if (!authForBlock(block, key, keyType)) { haltCard(); server.send(200, "text/plain", "Auth gagal."); return; }
  MFRC522::StatusCode status;
  if (op == "increment") status = rfid.MIFARE_Increment(block, amount);
  else status = rfid.MIFARE_Decrement(block, amount);
  if (status == MFRC522::STATUS_OK) status = rfid.MIFARE_Transfer(block);
  haltCard();
  if (status != MFRC522::STATUS_OK) { server.send(200, "text/plain", "Operasi gagal: " + String(rfid.GetStatusCodeName(status))); return; }
  server.send(200, "text/plain", "Berhasil " + op + " blok " + String(block) + " sebesar " + String(amount));
}

void handleValueRead() {
  if (!server.hasArg("block") || !server.hasArg("key")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  int block = server.arg("block").toInt();
  byte key[6];
  if (!parseHexKey(server.arg("key"), key)) { server.send(200, "text/plain", "Format key salah."); return; }
  String keyType = server.arg("keytype");
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  if (!authForBlock(block, key, keyType)) { haltCard(); server.send(200, "text/plain", "Auth gagal."); return; }
  long value;
  MFRC522::StatusCode status = rfid.MIFARE_GetValue(block, &value);
  haltCard();
  if (status != MFRC522::STATUS_OK) { server.send(200, "text/plain", "Baca value gagal: " + String(rfid.GetStatusCodeName(status))); return; }
  server.send(200, "text/plain", "Value blok " + String(block) + " = " + String(value));
}

void handleBackupCard() {
  if (!server.hasArg("key")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  byte key[6];
  if (!parseHexKey(server.arg("key"), key)) { server.send(200, "text/plain", "Format key salah."); return; }
  String name = server.hasArg("name") ? server.arg("name") : "";
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  String uid = uidToString(&rfid.uid);
  if (name == "") name = uid;
  lastUID = uid;

  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, key, 6);
  String dump = "";
  byte buffer[18]; byte size = 18;
  int okBlocks = 0;
  for (byte sector = 0; sector < 16; sector++) {
    byte trailerBlock = sector * 4 + 3;
    MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
    for (byte b = 0; b < 4; b++) {
      byte block = sector * 4 + b;
      if (status == MFRC522::STATUS_OK) {
        MFRC522::StatusCode rs = rfid.MIFARE_Read(block, buffer, &size);
        if (rs == MFRC522::STATUS_OK) { dump += bytesToHex(buffer, 16); okBlocks++; }
        else dump += "----------------------------------------------";
      } else {
        dump += "----------------------------------------------";
      }
      dump += "|";
    }
  }
  haltCard();

  prefs.begin("backups", false);
  int count = prefs.getInt("count", 0);
  prefs.putString(("uid" + String(count)).c_str(), uid);
  prefs.putString(("name" + String(count)).c_str(), name);
  prefs.putString(("dump" + String(count)).c_str(), dump);
  prefs.putInt("count", count + 1);
  prefs.end();

  server.send(200, "text/plain", "Backup tersimpan: " + name + " (" + uid + "), " + String(okBlocks) + "/64 blok berhasil dibaca.");
}

void handleListBackups() {
  prefs.begin("backups", true);
  int count = prefs.getInt("count", 0);
  String out = "Total backup: " + String(count) + "\n\n";
  for (int i = 0; i < count; i++) {
    String uid = prefs.getString(("uid" + String(i)).c_str(), "");
    String name = prefs.getString(("name" + String(i)).c_str(), "");
    out += String(i + 1) + ". " + name + " - " + uid + "\n";
  }
  prefs.end();
  server.send(200, "text/plain", out);
}

void handleCloneCard() {
  if (!server.hasArg("name")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  String name = server.arg("name");
  prefs.begin("backups", true);
  int count = prefs.getInt("count", 0);
  String dump = "";
  bool found = false;
  for (int i = 0; i < count; i++) {
    if (prefs.getString(("name" + String(i)).c_str(), "") == name) {
      dump = prefs.getString(("dump" + String(i)).c_str(), "");
      found = true;
      break;
    }
  }
  prefs.end();
  if (!found) { server.send(200, "text/plain", "Backup dengan nama itu tidak ada."); return; }

  if (!waitCard()) { server.send(200, "text/plain", "Kartu target tidak terdeteksi."); return; }
  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, (byte*)"\xFF\xFF\xFF\xFF\xFF\xFF", 6);
  int written = 0;
  for (byte sector = 0; sector < 16; sector++) {
    byte trailerBlock = sector * 4 + 3;
    MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
    if (status != MFRC522::STATUS_OK) continue;
    for (byte b = 0; b < 3; b++) {
      byte block = sector * 4 + b;
      int idx = block * 33;
      if (idx + 32 > (int)dump.length()) continue;
      String blockHex = dump.substring(idx, idx + 32);
      byte data[16];
      if (blockHex.indexOf("-") >= 0) continue;
      if (!parseHexBlock(blockHex, data)) continue;
      MFRC522::StatusCode ws = rfid.MIFARE_Write(block, data, 16);
      if (ws == MFRC522::STATUS_OK) written++;
    }
  }
  haltCard();
  server.send(200, "text/plain", "Clone selesai, " + String(written) + " blok data ditulis dari backup " + name + ".");
}

void handleWriteUID() {
  if (!server.hasArg("uid")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  String uidHex = server.arg("uid");
  if (uidHex.length() != 8) { server.send(200, "text/plain", "UID harus 8 karakter hex (4 byte)."); return; }
  byte newUid[4];
  for (int i = 0; i < 4; i++) newUid[i] = strtol(uidHex.substring(i*2, i*2+2).c_str(), NULL, 16);
  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  bool ok = rfid.MIFARE_SetUid(newUid, (byte)4, true);
  haltCard();
  if (!ok) {
    server.send(200, "text/plain", "Gagal tulis UID, pastikan pakai kartu magic (UID changeable).");
    return;
  }
  server.send(200, "text/plain", "UID baru berhasil ditulis: " + uidHex);
}

void handleFormatCard() {
  String userKeyHex = server.hasArg("key") ? server.arg("key") : "";
  byte userKey[6];
  bool hasUserKey = parseHexKey(userKeyHex, userKey);

  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }
  int okSectors = 0;
  int failSectors = 0;
  byte zeroData[16] = {0};
  byte defaultTrailer[16] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0x80,0x69,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  for (byte sector = 0; sector < 16; sector++) {
    byte trailerBlock = sector * 4 + 3;
    bool authOk = false;
    if (hasUserKey) {
      MFRC522::MIFARE_Key k;
      memcpy(k.keyByte, userKey, 6);
      MFRC522::StatusCode st = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &k, &(rfid.uid));
      if (st == MFRC522::STATUS_OK) authOk = true;
      else rfid.PCD_StopCrypto1();
    }
    if (!authOk) authOk = authTrailerMultiKey(trailerBlock, nullptr);
    if (!authOk) { failSectors++; continue; }

    for (byte b = 0; b < 3; b++) {
      byte block = sector * 4 + b;
      if (sector == 0 && b == 0) continue;
      rfid.MIFARE_Write(block, zeroData, 16);
    }
    MFRC522::StatusCode ws = rfid.MIFARE_Write(trailerBlock, defaultTrailer, 16);
    if (ws == MFRC522::STATUS_OK) okSectors++; else failSectors++;
  }
  haltCard();
  server.send(200, "text/plain", "Format selesai, " + String(okSectors) + "/16 sektor dikembalikan ke default. " + String(failSectors) + " sektor gagal (key tidak dikenal).");
}

int buildURIRecord(byte *rec, String url, bool mb, bool me) {
  byte prefixCode = 0x00;
  String rest = url;
  const char* prefixes[] = {"http://www.","https://www.","http://","https://","tel:","mailto:"};
  byte codes[] = {0x01,0x02,0x03,0x04,0x05,0x06};
  for (int i = 0; i < 6; i++) {
    if (url.startsWith(prefixes[i])) { prefixCode = codes[i]; rest = url.substring(strlen(prefixes[i])); break; }
  }
  int len = rest.length();
  int p = 0;
  byte header = 0xD1;
  if (!mb) header &= ~0x80;
  if (!me) header &= ~0x40;
  rec[p++] = header;
  rec[p++] = 0x01;
  rec[p++] = len + 1;
  rec[p++] = 'U';
  rec[p++] = prefixCode;
  for (int i = 0; i < len; i++) rec[p++] = rest.charAt(i);
  return p;
}

int buildTextRecord(byte *rec, String text, bool mb, bool me) {
  int len = text.length();
  int p = 0;
  byte header = 0xD1;
  if (!mb) header &= ~0x80;
  if (!me) header &= ~0x40;
  rec[p++] = header;
  rec[p++] = 0x01;
  rec[p++] = len + 3;
  rec[p++] = 'T';
  rec[p++] = 0x02;
  rec[p++] = 'e'; rec[p++] = 'n';
  for (int i = 0; i < len; i++) rec[p++] = text.charAt(i);
  return p;
}

int buildAARRecord(byte *rec, String pkg, bool mb, bool me) {
  int len = pkg.length();
  int p = 0;
  byte header = 0xD4;
  if (!mb) header &= ~0x80;
  if (!me) header &= ~0x40;
  rec[p++] = header;
  rec[p++] = 15;
  rec[p++] = len;
  const char* type = "android.com:pkg";
  for (int i = 0; i < 15; i++) rec[p++] = type[i];
  for (int i = 0; i < len; i++) rec[p++] = pkg.charAt(i);
  return p;
}

void handleWriteNDEF() {
  String type = server.hasArg("type") ? server.arg("type") : "url";
  String f1 = server.hasArg("f1") ? server.arg("f1") : "";
  String f2 = server.hasArg("f2") ? server.arg("f2") : "";
  String f3 = server.hasArg("f3") ? server.arg("f3") : "";
  String aar = server.hasArg("aar") ? server.arg("aar") : "";

  String uriString = "";
  String textString = "";
  bool useText = false;

  if (type == "url") uriString = f1;
  else if (type == "text") { textString = f1; useText = true; }
  else if (type == "tel") uriString = "tel:" + f1;
  else if (type == "sms") uriString = "sms:" + f1 + (f2 != "" ? ("?body=" + f2) : "");
  else if (type == "email") uriString = "mailto:" + f1 + (f2 != "" || f3 != "" ? ("?subject=" + f2 + "&body=" + f3) : "");
  else if (type == "wifi") { textString = "WIFI:S:" + f1 + ";T:" + f3 + ";P:" + f2 + ";;"; useText = false; uriString = ""; }
  else if (type == "vcard") { textString = "BEGIN:VCARD\nVERSION:3.0\nFN:" + f1 + "\nTEL:" + f2 + (f3 != "" ? ("\nEMAIL:" + f3) : "") + "\nEND:VCARD"; useText = true; }

  if (type == "wifi") {
    String wifiUri = "WIFI:S:" + f1 + ";T:" + f3 + ";P:" + f2 + ";;";
    textString = wifiUri;
    useText = true;
  }

  bool hasAAR = aar.length() > 0;
  byte rec1[128];
  int rec1Len;
  if (useText) rec1Len = buildTextRecord(rec1, textString, true, !hasAAR);
  else rec1Len = buildURIRecord(rec1, uriString, true, !hasAAR);

  byte rec2[64];
  int rec2Len = 0;
  if (hasAAR) rec2Len = buildAARRecord(rec2, aar, false, true);

  byte payload[192] = {0};
  int ndefLen = rec1Len + rec2Len;
  int p = 0;
  if (ndefLen < 255) {
    payload[p++] = 0x03;
    payload[p++] = ndefLen;
  } else {
    payload[p++] = 0x03;
    payload[p++] = 0xFF;
    payload[p++] = (ndefLen >> 8) & 0xFF;
    payload[p++] = ndefLen & 0xFF;
  }
  memcpy(payload + p, rec1, rec1Len); p += rec1Len;
  if (hasAAR) { memcpy(payload + p, rec2, rec2Len); p += rec2Len; }
  payload[p++] = 0xFE;

  if (p > 96) { server.send(200, "text/plain", "Data terlalu besar, muat sampai sekitar 90 byte saja."); return; }

  if (!waitCard()) { server.send(200, "text/plain", "Kartu tidak terdeteksi."); return; }

  if (!authTrailerMultiKey(3, nullptr)) {
    haltCard();
    server.send(200, "text/plain", "Auth sektor 0 gagal. Kartu mungkin pakai key custom, coba Format Kartu dulu.");
    return;
  }

  byte mad[16] = {0x01,0x03,0xA0,0x0C,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03};
  rfid.MIFARE_Write(1, mad, 16);
  byte mad2[16] = {0};
  rfid.MIFARE_Write(2, mad2, 16);
  byte trailer0[16] = {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0x78,0x77,0x88,0xC1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  rfid.MIFARE_Write(3, trailer0, 16);
  byte trailerNdef[16] = {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0x78,0x77,0x88,0xC1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

  int blocksNeeded = (p + 15) / 16;
  int written = 0;
  byte padded[192] = {0};
  memcpy(padded, payload, p);

  int blockList[6] = {4,5,6,8,9,10};
  int trailerList[2] = {7,11};
  int bi = 0;
  for (int sectorAuth = 0; sectorAuth < 2 && bi < blocksNeeded; sectorAuth++) {
    if (!authTrailerMultiKey(trailerList[sectorAuth], nullptr)) { bi += 3; continue; }
    for (int j = 0; j < 3 && bi < blocksNeeded; j++, bi++) {
      MFRC522::StatusCode ws = rfid.MIFARE_Write(blockList[sectorAuth * 3 + j], padded + bi * 16, 16);
      if (ws == MFRC522::STATUS_OK) written++;
    }
    rfid.MIFARE_Write(trailerList[sectorAuth], trailerNdef, 16);
  }
  haltCard();
  server.send(200, "text/plain", "Kartu ditulis NDEF tipe " + type + ", " + String(written) + "/" + String(blocksNeeded) + " blok berhasil.\nCoba tap pakai NFC HP (bukan dari web ini).");
}

void handleSelfTest() {
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  String out = "Versi chip RC522: 0x" + String(version, HEX) + "\n";
  bool ok = rfid.PCD_PerformSelfTest();
  out += ok ? "Self test: LULUS" : "Self test: GAGAL";
  rfid.PCD_Init();
  server.send(200, "text/plain", out);
}

void handleDeleteCard() {
  if (!server.hasArg("uid")) { server.send(200, "text/plain", "Parameter kurang."); return; }
  String uid = server.arg("uid");
  uid.toUpperCase();
  prefs.begin("cards", false);
  int count = prefs.getInt("count", 0);
  String uids[50], names[50];
  int newCount = 0;
  bool deleted = false;
  for (int i = 0; i < count; i++) {
    String u = prefs.getString(("uid" + String(i)).c_str(), "");
    String n = prefs.getString(("name" + String(i)).c_str(), "");
    if (u == uid) { deleted = true; continue; }
    uids[newCount] = u;
    names[newCount] = n;
    newCount++;
  }
  prefs.clear();
  for (int i = 0; i < newCount; i++) {
    prefs.putString(("uid" + String(i)).c_str(), uids[i]);
    prefs.putString(("name" + String(i)).c_str(), names[i]);
  }
  prefs.putInt("count", newCount);
  prefs.end();
  server.send(200, "text/plain", deleted ? "Kartu " + uid + " dihapus." : "UID tidak ditemukan di daftar.");
}

void handleTestCard() {
  if (!waitCard()) {
    server.send(200, "text/plain", "Kartu tidak terdeteksi.");
    return;
  }
  String uid = uidToString(&rfid.uid);
  haltCard();
  lastUID = uid;

  prefs.begin("cards", true);
  int count = prefs.getInt("count", 0);
  String matchName = "";
  for (int i = 0; i < count; i++) {
    if (prefs.getString(("uid" + String(i)).c_str(), "") == uid) {
      matchName = prefs.getString(("name" + String(i)).c_str(), "");
      break;
    }
  }
  prefs.end();

  if (matchName != "") {
    beep(buzzerDurationMs);
    server.send(200, "text/plain", "COCOK: " + matchName + " (" + uid + ")");
  } else {
    server.send(200, "text/plain", "TIDAK COCOK. UID: " + uid);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  for (byte i = 0; i < 6; i++) {
    keyA.keyByte[i] = 0xFF;
    keyB.keyByte[i] = 0xFF;
  }

  SPI.begin();
  rfid.PCD_Init();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/read_uid", handleReadUID);
  server.on("/read_all", handleReadAll);
  server.on("/write_block", handleWriteBlock);
  server.on("/change_key", handleChangeKey);
  server.on("/save_card", handleSaveCard);
  server.on("/list_cards", handleListCards);
  server.on("/clear_cards", handleClearCards);
  server.on("/test_card", handleTestCard);
  server.on("/set_buzzer", handleSetBuzzer);
  server.on("/read_all_custom", handleReadAllCustom);
  server.on("/brute_keys", handleBruteKeys);
  server.on("/set_access", handleSetAccess);
  server.on("/value_init", handleValueInit);
  server.on("/value_op", handleValueOp);
  server.on("/value_read", handleValueRead);
  server.on("/backup_card", handleBackupCard);
  server.on("/list_backups", handleListBackups);
  server.on("/clone_card", handleCloneCard);
  server.on("/write_uid", handleWriteUID);
  server.on("/format_card", handleFormatCard);
  server.on("/write_ndef", handleWriteNDEF);
  server.on("/self_test", handleSelfTest);
  server.on("/delete_card", handleDeleteCard);
  server.on("/poll_uid", handlePollUID);
  server.on("/poll_test", handlePollTest);
  server.begin();
}

void loop() {
  server.handleClient();
}
