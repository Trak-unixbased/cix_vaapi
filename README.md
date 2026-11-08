# CIX VA-API Driver (`libcix_va_drv_video.so`)

Pilote d'accélération matérielle vidéo basé sur le standard **VA-API** (*Video Acceleration API*) pour les processeurs et puces graphiques/multimédia **CIX Technology** (notamment les SoC Armv9 CIX P1 / SKY1 / CD8160 embarqués sur des cartes comme la *Radxa Orion O6* ou l'*Orange Pi 6 Plus*).

---

## 1. Présentation du projet

Ce dépôt fournit l'implémentation en espace utilisateur (*userspace driver*) de la couche VA-API pour la VPU (*Video Processing Unit*) des puces CIX Technology. 

Il permet d'assurer l'interface standardisée entre l'API Linux VA-API (via `libva`) et le noyau Linux / matériel CIX, rendant l'accélération matérielle transparente pour les lecteurs multimédias, navigateurs et frameworks vidéo (`FFmpeg`, `GStreamer`, `mpv`, Chromium, etc.).

### Fonctionnalités supportées

* **Décodage matériel (Hardware Decode) :**
  * H.264 (AVC)
  * HEVC (H.265)
  * VP9
  * AV1
* **Encodage matériel (Hardware Encode) :**
  * H.264 (AVC)
  * HEVC (H.265)
* **Traitement vidéo (VPP - Video Post-Processing via CME) :**
  * Conversion de formats de pixels (ex. NV12, YUV420p)
  * Mise à l'échelle matérielle (*Scaling*)
  * Incrustation / Superposition (*Overlay*)

---

## 2. Prérequis

### Prérequis matériels
* Carte ou plateforme équipée d'un SoC CIX Technology (ex. CIX P1 / SKY1 / CD8160) en architecture **AArch64** (`arm64`).
* Module noyau VPU CIX activé et nœud DRM opérationnel (généralement `/dev/dri/renderD128` ou `/dev/dri/card0`).

### Prérequis logiciels & dépendances système
Sur une distribution basée sur Debian / Ubuntu (ex. Armbian Debian 13, Ubuntu 24.04/26.04 aarch64) :

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libdrm-dev \
    libva-dev \
    vainfo
```

> **Note :** Selon votre distribution, CIX fournit également des paquets dédiés `cix-libva2` et `cix-libva-dev` pour une intégration optimisée. Assurez-vous que les en-têtes VA-API (`va/va.h`, `va/va_drm.h`) sont disponibles.

---

## 3. Compilation

### Option A : Compilation native (directement sur la machine cible AArch64)

```bash
# Cloner le dépôt
git clone https://github.com/Trak-unixbased/cix_vaapi.git
cd cix_vaapi

# Créer le répertoire de build
mkdir build && cd build

# Configurer avec CMake
cmake -DCMAKE_BUILD_TYPE=Release ..

# Compiler
make -j$(nproc)
```

Pour un build de débogage sans optimisations :
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG="-g -O0" -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" ..
make -j$(nproc)
```

### Option B : Compilation croisée (*Cross-compilation* depuis un hôte x86_64)

Installez la chaîne de cross-compilation AArch64 :
```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

Générez les binaires en utilisant le fichier toolchain du projet :
```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cc.cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Le fichier compilé produit est la bibliothèque partagée **`libcix_va_drv_video.so`**.

---

## 4. Installation et Configuration

### 4.1. Copie de la bibliothèque

Copiez la bibliothèque partagée dans le répertoire des pilotes VA-API du système (selon l'arborescence de votre distribution) :

```bash
# Répertoire standard Debian/Ubuntu multi-arch
sudo mkdir -p /usr/lib/aarch64-linux-gnu/dri/
sudo cp libcix_va_drv_video.so /usr/lib/aarch64-linux-gnu/dri/

# Ou vers /usr/local/lib si vous isolez les composants tiers
sudo mkdir -p /usr/local/lib/aarch64-linux-gnu/dri/
sudo cp libcix_va_drv_video.so /usr/local/lib/aarch64-linux-gnu/dri/
```

### 4.2. Configuration des liens symboliques et de `libva`

`libva` recherche généralement un pilote nommé `<nom_du_driver>_drv_video.so`. Créez un lien symbolique pour assurer la compatibilité automatique :

```bash
cd /usr/lib/aarch64-linux-gnu/dri/
sudo ln -sf libcix_va_drv_video.so cix_drv_video.so
```

### 4.3. Variables d'environnement

Ajoutez les variables suivantes dans votre fichier d'environnement (ex. `/etc/environment` ou `~/.bashrc`) :

```bash
# Spécifier le pilote VA-API à charger
export LIBVA_DRIVER_NAME=cix

# Définir le chemin si vous avez installé dans un dossier personnalisé
export LIBVA_DRIVERS_PATH=/usr/lib/aarch64-linux-gnu/dri
```

Appliquez les changements :
```bash
source ~/.bashrc
```

---

## 5. Mise en œuvre et validation

### 5.1. Vérification avec `vainfo`

Exécutez `vainfo` pour contrôler que le pilote s'initialise correctement et vérifier la liste des profils d'encodage/décodage reconnus :

```bash
vainfo --display drm --device /dev/dri/renderD128
```

Vous devriez observer une sortie mentionnant `cix` ou `libcix_va_drv_video.so`, suivie des profils H.264, HEVC, VP9 et AV1 avec les points d'entrée `VAEntrypointVLD` (décodage) et `VAEntrypointEncSlice` (encodage).

### 5.2. Test de décodage avec FFmpeg

Pour décoder une vidéo H.264 ou HEVC en exploitant la VPU :

```bash
ffmpeg -hwaccel vaapi \
       -vaapi_device /dev/dri/renderD128 \
       -i test_input.mp4 \
       -f null -
```

### 5.3. Test d'encodage avec FFmpeg

Pour encoder un flux brut ou ré-encoder une vidéo en H.264 matériel :

```bash
ffmpeg -vaapi_device /dev/dri/renderD128 \
       -i input.mp4 \
       -vf 'format=nv12,hwupload' \
       -c:v h264_vaapi \
       -b:v 5M \
       output_cix_h264.mp4
```

Pour du HEVC/H.265 :
```bash
ffmpeg -vaapi_device /dev/dri/renderD128 \
       -i input.mp4 \
       -vf 'format=nv12,hwupload' \
       -c:v hevc_vaapi \
       output_cix_hevc.mp4
```

### 5.4. Lecture avec mpv

```bash
mpv --hwdec=vaapi --vo=gpu test_input.mp4
```

---

## 6. Outils de diagnostic et débogage

Le pilote intègre des mécanismes d'observation configurables par variables d'environnement.

### Niveaux de journalisation (`CIX_VAAPI_LOG`)

Niveaux disponibles :
* `0` : ERROR
* `1` : WARNING (défaut)
* `2` : INFO
* `3` : DEBUG

Exemple pour activer les logs détaillés :
```bash
export CIX_VAAPI_LOG=3
vainfo
```

### Outils de Dump (Analyse de flux)

Vous pouvez extraire les flux bruts et les trames décodées à des fins de débogage :
* Trames décompressées : `cix-vaapi-dump-%Y%m%d-%H%M%S.yuv`
* Flux compressé binaire : `cix-vaapi-dump-%Y%m%d-%H%M%S.bin`

---

## 7. Licence

Ce projet est distribué sous licence open source **Apache License 2.0**. Consultez le fichier `LICENSE` pour plus de détails.

# CIX VAAPI Driver
## How to build
```bash
mkdir build
cd build
cmake ..
make
```

Copy the generated `libcix_va_drv_video.so` to `/usr/local/lib/aarch64-linux-gnu/dri/`

### Cross-compile for aarch64
```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cc.cmake ..
make
```

### Debug build and turn off optimization
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG="-g -O0" -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" ..
```

## Supported codecs
- **Decode**: H.264, HEVC, VP9, AV1
- **Encode**: H.264, HEVC
- **VPP**: Format conversion, scaling, overlay (via CME)

## Log tool
Log levels
- 0 - ERROR
- 1 - WARNING
- 2 - INFO
- 3 - DEBUG

Default WARNING

Set environment variable `CIX_VAAPI_LOG` to change log level.

```bash
export CIX_VAAPI_LOG=3
```

## Dump tool
Use env variables to enable and disable dump feature. Dump file name will be generated in following pattern.

- cix-vaapi-dump-%Y%m%d-%H%M%S.yuv for frame dump;
- cix-vaapi-dump-%Y%m%d-%H%M%S.bin for bitstream dump.
### Dump bitstream
```bash
export CIX_VAAPI_DUMP_BITS=1
```
### Dump frame buffers
```bash
export CIX_VAAPI_DUMP_FRAME=1
```
