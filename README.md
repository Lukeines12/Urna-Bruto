# 🗳️ Urna Eletrônica — ELE2IT / SENAI

**Autor:** Lukeines12 (Lemos)  
**Curso:** Técnico em Eletroeletrônica — 3º Termo  
**Matéria:** Sistemas Eletrônicos Digitais (SELDI)  
**MCU:** ESP32 · **Linguagem:** C (ESP-IDF)  

---

## 📋 Descrição

Sistema de votação eletrônica com:

- **Teclado matricial 4×4** para entrada do número do candidato
- **Display LCD 16×2** (modo paralelo 4 bits) para feedback ao eleitor
- **2 turnos** configuráveis
- **Envio de resultados** em tempo real para a plataforma **Wegnology** via HTTP

---

## 🔌 Pinagem

### LCD 16×2 (modo 4 bits)

| Sinal LCD | GPIO ESP32 | Cor sugerida |
|-----------|-----------|--------------|
| RS        | 19        | Verde        |
| EN        | 18        | Amarelo      |
| D4        | 5         | Laranja      |
| D5        | 17        | Azul         |
| D6        | 16        | Roxo         |
| D7        | 4         | Marrom       |
| VDD       | 3.3V      | Vermelho     |
| VSS / RW  | GND       | Preto        |
| A (LED+)  | 3.3V      | Vermelho     |
| K (LED-)  | GND       | Preto        |
| V0        | Pot. / R  | Cinza        |

> Use um resistor de **220Ω** no pino V0 para contraste fixo.

### Teclado Matricial 4×4

| Pino Teclado | GPIO ESP32 |
|-------------|-----------|
| R1          | 13        |
| R2          | 12        |
| R3          | 14        |
| R4          | 27        |
| C1          | 26        |
| C2          | 25        |
| C3          | 33        |
| C4          | 32        |

---

## 🎮 Como usar

| Tecla | Função                                  |
|-------|-----------------------------------------|
| `0–9` | Digita o número do candidato            |
| `#`   | **Confirma** o voto                     |
| `*`   | **Cancela** / volta ao início           |
| `C`   | **Apaga** o último dígito               |

**Fluxo:**

```
[Boas-vindas] → [Digite 2 dígitos] → [Exibe candidato] → [# confirma / * cancela]
     ↑                                                              |
     └──────────────── próximo eleitor ────────────────────────────┘
```

---

## 🗳️ Candidatos cadastrados

| Número | Nome     |
|--------|----------|
| 13     | Lula     |
| 22     | Flavio   |
| 14     | Renan    |
| 27     | Zema     |
| 33     | Daciolo  |
| 12     | Lemos    |
| 00     | BRANCO   |

> Número não encontrado = **VOTO NULO** (não é contabilizado).

---

## ⚙️ Configuração

Edite as seguintes linhas em `main/main.c`:

```c
#define WIFI_SSID      "SUA_REDE_WIFI"
#define WIFI_PASSWORD  "SUA_SENHA_WIFI"

#define WEGNOLOGY_DEVICE_ID  "SEU_DEVICE_ID"
#define WEGNOLOGY_ACCESS_KEY "SUA_ACCESS_KEY"

#define VOTOS_POR_TURNO 5   // quantos votos encerram um turno
```

---

## 🏗️ Compilar (ESP-IDF)

```bash
# 1. Definir target
idf.py set-target esp32

# 2. Compilar
idf.py build

# 3. Gravar no hardware
idf.py -p COMx flash monitor
```

---

## 🔬 Simular no Wokwi

1. Copie `wokwi/diagram.json` e `wokwi/wokwi.toml` para a raiz do projeto.
2. No VS Code, instale a extensão **Wokwi for VS Code**.
3. Compile com `idf.py build`.
4. Pressione `F1 → Wokwi: Start Simulator`.

---

## 📡 Integração Wegnology

Os dados são enviados via **HTTP POST** para o endpoint de estado do dispositivo:

```
POST https://api.app.wnology.io/applications/{id}/devices/{id}/state
Header: losant-access-key: <sua chave>
Body:   {"data": {"Lula": 3, "Flavio": 1, ..., "turno": 2}}
```

O envio ocorre **automaticamente** ao término do último turno.

---

## 📁 Estrutura do repositório

```
urna-eletronica/
├── main/
│   ├── main.c           ← código principal
│   └── CMakeLists.txt
├── wokwi/
│   ├── diagram.json     ← circuito Wokwi
│   └── wokwi.toml       ← config simulador
├── CMakeLists.txt
└── README.md
```

---

*Projeto individual — Curso Técnico em Eletroeletrônica · SENAI*
