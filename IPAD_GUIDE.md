# Guia d'Ús amb iPad - Web Interface

## 🎨 Interfície Web Tàctil

La Drum Machine té una interfície web completa optimitzada per iPad que et permet:
- ✅ Tocar els 16 pads amb el dit
- ✅ Canviar entre kits
- ✅ Ajustar velocity en temps real
- ✅ Veure stats (CPU, kit actual)
- ✅ Control responsive i fluid

## 📱 Configuració

### Opció 1: Connectar a la teva WiFi (Recomanat)

**Al codi Arduino** (`DrumMachine_ESP32S3.ino`):

```cpp
// Opció 1: Connectar a la teva WiFi
#define WIFI_MODE_STATION  // Descomenta aquesta línia
#define WIFI_SSID "EL_TEU_WIFI"
#define WIFI_PASSWORD "LA_TEVA_PASSWORD"
```

**Accés des de l'iPad:**
1. Connecta l'iPad a la mateixa WiFi
2. Obre Safari
3. Navega a: `http://drummachine.local`
4. O usa la IP que mostra el Serial Monitor: `http://192.168.x.x`

### Opció 2: Access Point (sense WiFi)

**Al codi Arduino**:

```cpp
// Opció 2: Crear Access Point propi
#define WIFI_MODE_AP  // Descomenta aquesta línia
#define AP_SSID "DrumMachine"
#define AP_PASSWORD "drummachine123"
```

**Accés des de l'iPad:**
1. A l'iPad: Settings → WiFi
2. Connecta a la xarxa "DrumMachine"
3. Password: "drummachine123"
4. Obre Safari
5. Navega a: `http://192.168.4.1`

## 🎮 Ús de la Interfície

### Pantalla Principal

```
┌─────────────────────────────────────┐
│     🥁 Drum Machine                 │
│   Kit: 808 Classic    CPU: 12.5%   │
├─────────────────────────────────────┤
│   ◀ Prev    Kit 1/3    Next ▶      │
├─────────────────────────────────────┤
│  ┌────┬────┬────┬────┐              │
│  │ 1  │ 2  │ 3  │ 4  │              │
│  │Kick│Snr │HHat│Clap│              │
│  ├────┼────┼────┼────┤              │
│  │ 5  │ 6  │ 7  │ 8  │              │
│  │Tom1│Tom2│Tom3│Crsh│              │
│  ├────┼────┼────┼────┤              │
│  │ 9  │ 10 │ 11 │ 12 │              │
│  │Ride│OpHH│Cow │Rim │              │
│  ├────┼────┼────┼────┤              │
│  │ 13 │ 14 │ 15 │ 16 │              │
│  │Clav│Mara│Shak│Perc│              │
│  └────┴────┴────┴────┘              │
├─────────────────────────────────────┤
│  Velocity: ━━━━━●━━━  100          │
└─────────────────────────────────────┘
```

### Controls

**Pads:**
- Tap per triggerar el sample
- Visual feedback quan toques
- Cada pad mostra el número i el nom

**Kit Selector:**
- "◀ Prev" - Kit anterior
- "Next ▶" - Kit següent
- Centre mostra kit actual

**Velocity Slider:**
- Arrossega per ajustar la força (30-127)
- Valor es mostra a la dreta

## ⚙️ Configuració Avançada

### IP Estàtica (Opcional)

Si vols una IP fixa, afegeix al codi abans de `WiFi.begin()`:

```cpp
IPAddress local_IP(192, 168, 1, 100);  // La IP que vols
IPAddress gateway(192, 168, 1, 1);     // Gateway del router
IPAddress subnet(255, 255, 255, 0);

WiFi.config(local_IP, gateway, subnet);
WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
```

### mDNS (Hostname)

Per defecte pots accedir amb: `http://drummachine.local`

Per canviar el nom:
```cpp
webInterface.begin(WIFI_SSID, WIFI_PASSWORD, "elnomquevulguis");
// Accés: http://elnomquevulguis.local
```

### Afegir a Home Screen (iOS)

1. Obre la web a Safari
2. Tap el botó de compartir
3. "Add to Home Screen"
4. Ja tens una app nativa!

## 🎹 Funcionalitats de la Web

### API Endpoints

La interfície web usa aquests endpoints:

**GET `/`** - Pàgina principal
- HTML amb interfície tàctil

**POST `/trigger`** - Triggerar pad
- Paràmetres: `pad` (0-15), `velocity` (0-127)
- Exemple: `/trigger?pad=0&velocity=100`

**POST `/kit`** - Canviar kit
- Paràmetre: `kit` (0-N)
- Exemple: `/kit?kit=1`

**GET `/status`** - Obtenir estat
- Retorna JSON amb: voices, cpu, currentKit, kitCount, kitName

### Crides AJAX Personalitzades

Si vols fer la teva pròpia app:

```javascript
// Triggerar pad 0 amb velocity 127
fetch('http://192.168.1.100/trigger', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'pad=0&velocity=127'
})
.then(r => r.json())
.then(data => console.log(data));

// Obtenir estat
fetch('http://192.168.1.100/status')
    .then(r => r.json())
    .then(data => {
        console.log('Kit actual:', data.kitName);
        console.log('CPU:', data.cpu + '%');
    });
```

## 🔧 Troubleshooting

### No puc connectar a la web

1. **Verifica WiFi/AP**: Comprova que l'ESP32 està connectat
   - Mira Serial Monitor per la IP
   - Comprova LED WiFi (si n'hi ha)

2. **Ping la IP**:
   ```bash
   ping 192.168.1.100
   ```

3. **Prova mDNS**:
   - Safari: `http://drummachine.local`
   - Si no funciona, usa IP directa

4. **Firewall**: Assegura't que no bloqueja port 80

### La web es veu malament a l'iPad

1. **Refresca**: Safari → Reload
2. **Neteja cache**: Settings → Safari → Clear History
3. **Mode Private**: Obre en navegació privada

### Els pads no responen

1. **Verifica consola**: Safari → Develop → Show Web Inspector
2. **Comprova Serial Monitor**: Veuràs si arriben les peticions
3. **Timeout**: Pot ser que la xarxa sigui lenta

### Latència alta

1. **WiFi Signal**: Acosta't al router/ESP32
2. **Mode AP**: Més ràpid que WiFi Station
3. **Xarxa 5GHz**: Millor que 2.4GHz si és possible

## 📊 Rendiment

**Latència típica:**
- WiFi local: 10-30ms
- Access Point: 5-15ms
- Touch → Audio: 20-50ms total

**Xarxa:**
- Ample de banda necessari: < 1 KB/s
- Concurrent users: Fins a 4 iPads simultàniament

## 🎨 Personalització CSS

Pots modificar l'aparença editant `generateCSS()` a `WebInterface.cpp`:

```cpp
// Canviar colors del gradient
background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);

// A per exemple:
background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
```

**Colors recomanats:**
- Sunset: `#ff9a56 0%, #ff6a88 100%`
- Ocean: `#2e3192 0%, #1bffff 100%`
- Forest: `#0ba360 0%, #3cba92 100%`
- Night: `#2c3e50 0%, #3498db 100%`

## 🔐 Seguretat

### Password per AP

```cpp
#define AP_PASSWORD "un_password_segur_123"
```

### Basic Auth (Opcional)

Afegeix al `WebInterface.cpp`:

```cpp
void WebInterface::handleRoot() {
  if (!server.authenticate("admin", "password")) {
    return server.requestAuthentication();
  }
  server.send(200, "text/html", generateHTML());
}
```

## 📱 Apps Alternatives

### Utilitzar amb MIDI sobre WiFi

Si afegeixes MIDI, pots usar apps com:
- **TouchOSC** - Control MIDI/OSC
- **Lemur** - Interfícies personalitzables
- **MIDI Designer** - Disseny lliure

### Control amb Python

```python
import requests

# Triggerar pad
requests.post('http://192.168.1.100/trigger', 
              data={'pad': 0, 'velocity': 100})

# Canviar kit
requests.post('http://192.168.1.100/kit', 
              data={'kit': 1})

# Obtenir estat
status = requests.get('http://192.168.1.100/status').json()
print(f"Kit: {status['kitName']}, CPU: {status['cpu']}%")
```

## 🎵 Tips d'Ús

1. **Afegeix a Home Screen** per accés ràpid
2. **Mode Landscape** funciona millor per tocar
3. **Desactiva Auto-Lock** per sessions llargues
4. **Baixa Brightness** per estalviar bateria
5. **Usa WiFi 5GHz** per menys latència

Gaudeix tocant des de l'iPad! 🎶
