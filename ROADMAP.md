# Foxiwium OS — дорожная карта и состояние проекта

Этот файл — единый источник правды между сессиями. Веди его при каждом крупном
изменении. Файл на русском, т.к. проект ведётся на русском.

**Пароль sudo** - breadyouroute4

---

## 1. Как собрать, запустить и отладить

```bash
make kernel && make iso                      # сборка
qemu-system-x86_64 -cdrom build/foxiwium.iso -m 512M \
  -accel tcg,thread=single -display none \
  -debugcon file:/tmp/fox_dbg.log -nic user,model=rtl8139
tr -d '\0' < /tmp/fox_dbg.log | grep -E "..."   # чтение лога (debugcon отдаёт NUL)
```

- QEMU 8.2.2 (Debian). `-accel tcg,thread=single` — без KVM (гостевой таймер не
  мешает). `-debugcon file:...` = порт 0xE9.
- Для XHCI/USB добавить: `-device qemu-xhci` (и `-device usb-net`,
  `-device usb-kbd`, `-device usb-tablet` для теста перечисления).
- Сборка: `-Werror -O2 -mno-sse -mno-sse2`, фриландинг, без исключений/RTTI.
  **`-Werror`**: никаких неиспользуемых функций/переменных — иначе не соберётся.
- Стиль кода: почти всё — `inline`-функции в заголовках, статические глобалы.
- `dbg(...)`/`dbgc(c)` пишут в порт 0xE9 (debugcon). `dbgh(v, digits)` — hex.
- Kernel identity-mapped (pmm возвращает физический==виртуальный адрес), куча на
  `0xFFFF800040000000`. Для MMIO-устройств маппить через `vmm::map_page()`.

---

## 2. Что работает сейчас

### Сеть (в QEMU через slirp NAT)
- RTL8139 PCI-драйвер: RX-кольцо 8192 байта, TX-буферы 4 шт.
- Стек: ARP, IPv4, ICMP echo (ping), UDP, DNS-резолвер, TCP (открытие/закрытие/
  приём/передача), HTTP/1.1 GET.
- Проверено в QEMU: `dns_resolve("example.com")` → IP; `http_get(...)` → 511
  байт HTML; браузер грузит `http://example.com/` (559 байт, `<!doctype`);
  терминал `curl` печатает тело (559 байт), `wget` сохраняет `index.html`,
  `ping example.com` / `ping 10.0.2.2` — реальный ICMP.

### Терминальные сетевые команды (больше НЕ заглушки)
- `ping <host|ip>` — резолвит имя через `net::dns_resolve()`, потом `net::ping`.
- `curl <url>` — `net::http_get()` + вывод тела в stdout оболочки.
- `wget <url>` — скачивает и сохраняет файл в текущую директорию VFS.

### Ключевой фикс RX-гонки (QEMU slirp)
QEMU доставляет пакеты асинхронно из своего главного цикла. Гостевой busy-wait
без выхода в QEMU крутится в чистом гостевом коде, и ответ никогда не приходит.
Поэтому в `rtl8139::receive()` (`kernel/drivers/rtl8139.cpp:218`):
- пакета нет → 4× `port::inl(io_base + REG_CMD)` (PIO-выход), вернуть -1;
- ROK есть → 8× PIO-выход перед копированием (пусть DMA докончится).

Регистры: REG_CMD=0x37, REG_CAPR=0x34, REG_RBSTART=0x30, REG_MAC0=0x00,
REG_RCR=0x44, REG_IMR=0x3C. RX: header[0] bit0 = ROK, header[1] = размер(+CRC),
CAPR пишется `rx_ptr - 0x10`.

---

## 3. Подтверждённые баги (по приоритету)

1. **ARP-truthy баг** (исправлен, `kernel/drivers/net.cpp http_dns_step`):
   `arp_lookup()` возвращает 1 (попадание) / −1 (промах), а `-1` — truthy в C,
   поэтому `if (arp_lookup(...))` был всегда истинен. Первый DNS-запрос уходил
   с пустым ARP-кэшем, `ip_send` его молча дропал, повторной отправки не было →
   `http_get=-3`. Фикс: `== 1`. Плюс `http_dns_step` теперь шлёт первый ARP
   сразу (флаг `dns_arp_started`), как старый `arp_resolve`.
2. **`parse_url` отклоняет `https://`** (`kernel/drivers/net.cpp:745`):
   `return -1` на схему https — «нет TLS». Пока TLS не сделан, остаётся так.
3. **`http_get` не следует редиректам 301/302** — из-за этого DuckDuckGo
   (`html.duckduckgo.com/html/?q=...`) недоступен по http: DDG принудительно
   отдаёт `301 → https`. Фикс: парсить заголовок `Location:` и повторять
   запрос (макс. ~5 переходов). Заголовки заканчиваются на `\r\n\r\n`
   (`find_header_end`, net.cpp:785).
4. **Нет кнопки WiFi** в панели. Кнопка питания нарисована в `draw_panel()`
   (`kernel/main.cpp:475`, клик на `main.cpp:1821`, x = `fw - 98`). Кнопку WiFi
   нужно добавить слева от неё (например x = `fw - 138`), с выпадающим списком
   сетей и индикатором подключения. Самих сетей нет (см. раздел 6).
5. **Нет TLS/HTTPS** — почти весь интернет недоступен (см. раздел 5). Поиск
   сделан в обход через Bing RSS (см. ниже).

---

## 4. Архитектура сетевого стека (для справки)

- `kernel/drivers/rtl8139.h/.cpp` — NIC. `receive()/send()`.
- `kernel/drivers/net.h/.cpp` — протоколы. Публичный API:
  `init, ready, poll, get_mac, get_ip, get_gateway, ip_to_str, parse_ip,
  ping(ip,max_iter), dns_resolve(host,&ip,max_iter),
  tcp_open/send/recv/peer_closed/close, http_begin(url,out,max_out) +
  http_poll(budget) + http_cancel (асинхронный HTTP),
  http_get(url,out,max_out,max_iter) — синхронная обёртка,
  get_tx_count, get_rx_count`.
- Коды ошибок HttpJob: −1 нет NIC, −2 плохой URL/scheme (https), −3 DNS fail,
  −4 TCP fail, −5 таймаут.
- Асинхронный HTTP: `http_begin` (фазы 1 DNS, 2 TCP, 3 приём), `http_poll(budget)`
  делает `budget` шагов за кадр; браузер зовёт `browser::update()` в main-loop
  (main.cpp:1580) после `net::poll()`.
- DNS-фикс: глобальный `dns_pending_id` (сравнение ID в `dns_handle`), и вызов
  `dns_handle(udp + 8, len - 8)` (начинается с DNS-заголовка, не с UDP).
- `poll_internal()` (net.cpp) разбирает пакеты; main-loop зовёт `net::poll()`
  (main.cpp ~1597). busy-wait-циклы внутри http_get/tcp/dns/arp зовут
  `poll_internal()` сами.
- Рейс QEMU slirp (async-доставка RX): `receive()` и `send()` делают PIO-выходы
  (чтение `REG_CMD`); после TX — 8 выходов, при пустом кольце — 8, при ROK — 16.
  Без них busy-poll спиннился в чистом guest-коде и ответ не приходил.
- Браузер `kernel/apps/browser.h`: асинхронный `load()` → `http_begin`,
  `update()` → `http_poll`; `navigate()` (browser.h:723) — поиск/URL/локальный
  файл; `show_error()` — «Error code: -N».

### Поиск в браузере (работает без TLS)

- Ввод в адресной строке, не похожий на URL (без `://`, без точки-без-пробелов)
  → поисковый запрос: `url_encode()` → `http://www.bing.com/search?q=…&format=rss`.
- Bing отдаёт RSS по чистому http (200, ~4 КБ, прямые ссылки без редиректов):
  `www.bing.com/search?q=test&format=rss` → `<item><title>…</title><link>URL</link>…`.
  Проверено: 10 результатов, 4081 байта, стабильно.
- DuckDuckGo НЕ годится: `html.duckduckgo.com` и `lite.duckduckgo.com`
  принудительно отдают `301 → https` (нужен TLS). Google по http — консент-стена,
  Mojeek — пустая JS-оболочка, Yandex — капча, Brave — https-редирект.
- `rss_to_html()` (browser.h) конвертирует RSS в `<a href=…>`; `update()` зовёт
  его для `s.current`, содержащего `format=rss`.
- Результаты ссылаются на `https://…` → клик даст «Unable to load −2» до TLS.

---

## 5. Дорожная карта (порядок утверждён пользователем)

1. **Интернет** (весь, а не только example.com):
   a. ✔ URL-баг починен (`url_is_http` + https, асинхронный HTTP);
   a2. ✔ **поиск в браузере** уже работает без TLS: адресная строка →
       Bing RSS (`www.bing.com/search?q=…&format=rss`), результаты — ссылки.
   b. редиректы 301/302 в `http_get` (нужны: DDG и почти все сайты
      отдают 301/302, включая http→https);
   c. **TLS/HTTPS** — самое ценное: почти весь интернет это HTTPS.
      План TLS: SHA-256 → AES-128-GCM (или AES-CBC+HMAC) → RSA/ECC → handshake
      TLS 1.2 (минимум) → проверка сертификатов (доверенное хранилище —
      отдельный вопрос). Долго, по этапам, но даёт максимум пользы.
   d. HTTP/2, JS/CSS-движок — когда-нибудь, за горизонтом.
2. **Работа с файлами** (VFS): расширить права, редактирование, копирование
   между разделами, сохранять вложения и т.п.
3. **Терминал**: доработка (Xandr).
4. **Настройки**: GUI-апплет настроек.
5. **TTY** (виртуальные консоли).
6. **Экраны ошибок**: Kernel Panic! синий/чёрный экран как в Linux.
7. **Звуки, текстуры** и прочий мультимедиа.

Порядок жёсткий: интернет → файлы → терминал → настройки → TTY → паника →
звук/текстуры.

---

## 6. План XHCI + USB WiFi (выбранное направление)

Цель: реальное железо + USB WiFi + подключение к роутеру. Выбран вариант
«USB XHCI стек + WiFi» (не TLS, не e1000 — решение пользователя).

Сейчас в ядре НЕТ USB вообще: ни XHCI/EHCI, ни классов устройств. Путь:

### Фаза A — XHCI-контроллер (в QEMU)
- PCI-скан по класс-коду `0x0C0330` (QEMU: `qemu-xhci` = 1B36:000D,
  `nec-usb-xhci` = 1033:0194). Сейчас PCI-скан только шины 0 (как у RTL8139).
- BAR0 — 64-битный MMIO BAR (BAR0+BAR1). Включить mem+bus master
  (PCI cmd 0x04 |= 0x6). Замапить через `vmm::map_page()` на
  `0xFFFF800001000000` (в стороне от кучи на 0xFFFF800040000000).
- Капы: CAPLENGTH(0x00), HCSPARAMS1(0x04: MaxSlots[0:7], MaxIntrs[8:15],
  MaxPorts[24:31]), HCCPARAMS1(0x10: CSZ=bit0, 64-bit=bit2), RTSOFF(0x18),
  DBOFF(0x14).
- Операционные: USBCMD(0x00: RS=0, HCRST=1, INTE=2), USBSTS(0x04: HCH=0,
  CNR=8), CRCR(0x10, 64-bit), DCBAAP(0x18, 64-bit), CONFIG(0x20).
- Сброс: USBCMD.HCRST=1, ждать самоочистки и USBSTS.CNR=0, HCH=1.
- Кольца (каждое — страница pmm::alloc_page()):
  - command ring 256 TRB × 16 байт; CRCR = PTR | RCS(1). В QEMU одинарный
    сегмент заворачивается вручную (toggle cycle) — без Link TRB; на реальном
    железе нужен Link TRB.
  - event ring 256 TRB; ERST = 1 сегмент (ERSTBA 64-bit + ERSTSZ=1) на
    ручтиме: Interrupter[0] на RTSOFF+0x20: ERSTSZ(0x08), ERSTBA(0x10),
    ERDP(0x18). Начальный cycle=1.
  - DCBAAP: массив MaxSlots+1 × 8 байт (страница).
- Запуск: CONFIG = MaxSlots, USBCMD.RS=1, ждать сброса HCH.
- Порта: PORTSC при op+0x400+(n-1)*0x10. PP(8)=питание, PR(4)=reset, ждать
  PRC(9), скорость Speed[20:23] (1=FS,2=LS,3=HS,4=SS), CCS(0).
- Команды через command ring + Doorbell[0]=0 (DBOFF):
  - Enable Slot (TRB type 0) → event вернёт Slot ID (code 1 = Success);
  - Set Address (TRB type 3): input context = ICC(2 DWORD, Add=1<<8) + Slot
    Context; размер контекста: CSZ ? 64 : 32 байта. Slot ctx: DWORD0 =
    Route(0) | Speed<<20 | Port<<24; DWORD1 = MaxExitLat(0) | MPS<<16
    (FS/LS=8, HS=64, SS=512); DWORD3 = SlotID | ContextEntries(1)<<27.
    BSR(TRB d3 bit4) = 1 если скорость >= HS. DCBAAP[slot] = output ctx.
  - Потом контрольные передачи (EP0) через Doorbell[slot]=0.
- Проверка в QEMU: `-device qemu-xhci` + `-device usb-kbd` (порты/перечисление),
  `-device usb-net` (CDC-сеть) для интеграции в net-стек.

### Фаза B — перечисление устройств + usb-net
- Address Device → Get Descriptor (дескриптор устройства/конфигурации) →
  Set Configuration. Контрольные передачи EP0 (Setup/Status/Data).
- USB-классы: hub, HID (kbd), CDC ECM (usb-net). Интеграция usb-net в
  существующий net-стек как второй NIC (или замену RTL8139).
- IRQ: пока polling (как NIC), потом MSI-X.

### Фаза C — реальное железо + WiFi
- Загрузка на реальном ПК (мультибут2 есть; проверить фреймбуфер/тайминги).
- Драйвер конкретного WiFi-чипа (зависит от модели адаптера пользователя! —
  уточнить: чип, vendor:device). Часто нужна загрузка firmware.
- Стек 802.11: скан сетей, ассоциация с роутером.
- WPA2/WPA3: AES-CCMP, handshake (EAPOL), PBKDF2. Это криптография.

---

## 7. История сессий (коротко)

- Сессия ранних отладок RTL8139: RX-гонка QEMU slirp, ROK раньше DMA-записи.
  Копировался нулевой кадр, пока не добавили PIO-выходы в receive().
- DNS-баги: (1) ID сравнения с уже инкрементированным `dns_id` → добавлен
  `dns_pending_id`; (2) `dns_handle` получал указатель на UDP-заголовок вместо
  DNS-пейлоада → `dns_handle(udp + 8, len - 8)`.
- Терминальные команды были заглушками («Name or service not known») →
  переписаны `cmd_ping/cmd_curl/cmd_wget` на реальную сеть.
- Отсутствие PIO-выхода в receive() при пустом кольце → busy-wait не получал
  ответов → добавлен yield в receive() и для пустого случая.
- HTTP-стек переписан на асинхронный (`http_begin/http_poll/http_cancel`),
  браузер грузит страницы по кадрам (browser::update() в main-loop).
- ARP-truthy баг: `if (arp_lookup())` всегда true (`-1` truthy) → первый DNS
  уходил с пустым ARP-кэшем и дропался в `ip_send`. Фикс `== 1` + немедленный
  первый ARP (`dns_arp_started`). До фикса `http_get=-3`, после — стабильно 511.
- Флаки (-3/-5) при убранных debug-принтах: debug (outb 0xE9) менял тайминги.
  Устойчивый фикс — PIO-выходы после TX в `send()` + усиление выходов в
  `receive()`. 6/6 прогонов 511/511.
- Поиск: DDG принудительно редиректит http→https → найден Bing RSS по чистому
  http (прямые ссылки, ~4КБ). `navigate()` → bing, `rss_to_html()` → ссылки.
- Решение пользователя: развивать USB XHCI + WiFi (не TLS в первую очередь,
  не e1000).

## 8. Куда смотреть (карта файлов)

- `kernel/drivers/rtl8139.{h,cpp}` — NIC.
- `kernel/drivers/net.{h,cpp}` — протоколы: http_begin:889, http_poll:1064,
  http_get:1082, http_cancel:909, arp_lookup:156, arp_send_request:179,
  dns_send_query, tcp_poll, parse_url:754 (https→−1).
- `kernel/apps/browser.h` — браузер (url_is_http:110, load:664, update:688,
  navigate:753, rss_to_html:210, поиск через Bing RSS).
- `kernel/apps/commands.h` — терминал (cmd_ping:1399, cmd_curl, cmd_wget).
- `kernel/main.cpp` — init (net::init:1450), панель (draw_panel:387, кнопка
  питания:475/1821), main-loop (net::poll + browser::update:1580, browser::handle_key).
- `kernel/mm/{vmm.h,pmm.h,heap.h}` — вирт./физ. память, куча.
- `kernel/fs/vfs.h` — VFS (макс. файл 4096 байт, MAX_NODES 512).
- `Makefile` — авто-глоб по kernel/**/*.cpp, новые .cpp подхватываются сами.
