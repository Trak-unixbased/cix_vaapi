# CIX VA-API Driver (`cix_drv_video.so`)

Pilote d'accélération matérielle vidéo basé sur le standard **VA-API** (*Video Acceleration API*) pour les SoC **CIX Technology** (CIX P1 / Sky1, CD8180 / CD8160) embarqués sur des cartes comme l'*Orange Pi 6 Plus* ou la *Radxa Orion O6*.

---

## 1. Présentation du projet

Ce dépôt fournit l'implémentation en espace utilisateur (*userspace driver*) de la couche VA-API pour la VPU (*Video Processing Unit*, Arm MVX « linlon ») des puces CIX. Le pilote pilote la VPU au travers de son interface **V4L2 stateful** (`/dev/videoN`) et alloue les surfaces dans les **DMA-BUF heaps** (`/dev/dma_heap/*`).

Il assure l'interface entre `libva` et le matériel CIX, rendant l'accélération transparente pour `FFmpeg`, `GStreamer`, `mpv`, Chromium, etc.

### Fonctionnalités

| Fonction | libva standard (Ubuntu 26.04, libva 2.23) | libva CIX (`cix-libva2`) |
|---|---|---|
| Décodage H.264 (Baseline contraint, Main, High) | oui | oui |
| Décodage VP9 (profils annoncés par la VPU) | oui | oui |
| Décodage HEVC / H.265 (Main, Main 10) | non annoncé (*) | oui |
| Décodage AV1 (Main) | non annoncé (*) | oui |
| Encodage H.264 / HEVC (8 bits) | oui | oui |
| Traitement vidéo (VPP : conversion, mise à l'échelle, rotation, incrustation) | si `libcme` est installée | si `libcme` est installée |

(*) Le décodeur *stateful* doit recevoir l'en-tête de séquence brut (SPS HEVC, *sequence header* et *frame header* AV1). Seule la libva de CIX transmet ces données au pilote (`VASequenceParameterBufferType`, `VADecPictureParameterBufferAV1Ext`). Avec la libva standard, ces profils ne sont donc plus annoncés : les applications basculent proprement sur le décodage logiciel au lieu de rester bloquées. La présence des extensions CIX est détectée automatiquement à la compilation.

L'encodeur est exposé sur `VAEntrypointEncSlice` **et** `VAEntrypointEncSliceLP` : `ffmpeg -c:v h264_vaapi` fonctionne sans `-low_power 1`.

---

## 2. Prérequis

### Matériel
* Carte équipée d'un SoC CIX P1 (CD8180 / CD8160) en **AArch64** (`arm64`).
* Pilote noyau de la VPU chargé (nœuds `/dev/video*` de type M2M multi-planar).
* Un nœud DRM (`/dev/dri/renderD128`) pour l'initialisation de libva.

### Logiciel (Debian / Ubuntu, ex. Ubuntu 26.04 LTS aarch64)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config dpkg-dev \
    libva-dev libdrm-dev vainfo
```

Optionnel (paquets du SDK CIX) :
* `cix-libva2` / `cix-libva-dev` : active le décodage HEVC et AV1 ;
* `libcme` (en-têtes dans `/usr/include/cme`) : active le traitement vidéo (VPP) ;
* `mvx-v4l2-controls.h` (dans `/usr/share/cix/include`) : utilisé s'il est présent, sinon les quelques définitions nécessaires sont fournies par `utils/tools/cix_va_compat.h`.

---

## 3. Paquet Debian (méthode recommandée)

Le script `packaging/build-deb.sh` compile le pilote et produit un paquet `cix-va-driver_<version>_arm64.deb`.

Sur la carte :

```bash
git clone https://github.com/Trak-unixbased/cix_vaapi.git
cd cix_vaapi
./packaging/build-deb.sh --install-deps      # installe aussi les dépendances de compilation
sudo apt install ./dist/cix-va-driver_*_arm64.deb
```

Depuis un PC x86_64 (compilation croisée) :

```bash
./packaging/build-deb.sh --cross --install-deps          # paquets multiarch arm64
# ou avec un rootfs arm64 :
./packaging/build-deb.sh --cross --sysroot /chemin/vers/rootfs-arm64
```

Options utiles : `--cme AUTO|ON|OFF`, `--mvx-include DIR`, `--lint`, `--no-strip`, `--clean`, `--install` (voir `--help`).

Le paquet installe :

| Fichier | Rôle |
|---|---|
| `/usr/lib/aarch64-linux-gnu/dri/cix_drv_video.so` | le pilote VA-API |
| `/usr/lib/udev/rules.d/70-cix-vaapi.rules` | accès à `/dev/dma_heap/*` pour le groupe `video` et l'utilisateur de la session |
| `/usr/lib/environment.d/90-cix-vaapi.conf` | `LIBVA_DRIVER_NAME=cix` pour les sessions graphiques |
| `/etc/profile.d/cix-vaapi.sh` | `LIBVA_DRIVER_NAME=cix` pour les shells (SSH, console) |

Après l'installation, fermez puis rouvrez la session et vérifiez que l'utilisateur appartient aux groupes `video` et `render` :

```bash
sudo usermod -aG video,render "$USER"
```

---

## 4. Compilation manuelle

### Compilation native (sur la carte)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build        # installe dans le répertoire « driverdir » de libva
```

Build de débogage :

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
```

### Compilation croisée (depuis un hôte x86_64)

```bash
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install -y g++-aarch64-linux-gnu libva-dev:arm64 libdrm-dev:arm64
cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cc.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j"$(nproc)"
```

Avec un rootfs arm64 : ajouter `-DCMAKE_SYSROOT=/chemin/vers/rootfs-arm64`.

Options CMake :

| Option | Défaut | Description |
|---|---|---|
| `CIX_VAAPI_CME` | `AUTO` | traitement vidéo via `libcme` (`AUTO`, `ON`, `OFF`) |
| `CIX_VAAPI_MVX_INCLUDE_DIR` | `/usr/share/cix/include` | répertoire de `mvx-v4l2-controls.h` |
| `CIX_VAAPI_DRIVERS_DIR` | `driverdir` de `libva.pc` | répertoire d'installation du pilote |
| `CIX_VAAPI_WERROR` | `OFF` | avertissements traités comme des erreurs |

La bibliothèque produite s'appelle directement **`cix_drv_video.so`** (nom attendu par libva avec `LIBVA_DRIVER_NAME=cix`) : le lien symbolique manuel n'est plus nécessaire.

---

## 5. Configuration

Le nœud DRM de la carte appartient au GPU : libva ne peut pas en déduire le nom du pilote vidéo. Il faut donc le sélectionner explicitement (fait automatiquement par le paquet) :

```bash
export LIBVA_DRIVER_NAME=cix
# uniquement si le pilote est installé hors du répertoire standard :
export LIBVA_DRIVERS_PATH=/usr/local/lib/aarch64-linux-gnu/dri
```

Les surfaces sont allouées dans `/dev/dma_heap/system` (puis `linux,cma` et `reserved` en repli). Un autre tas peut être imposé avec `CIX_VAAPI_DMA_HEAP=/dev/dma_heap/<nom>`.

---

## 6. Validation

### `vainfo`

```bash
vainfo --display drm --device /dev/dri/renderD128
```

La sortie doit mentionner `CIX media driver` et lister les profils avec `VAEntrypointVLD` (décodage), `VAEntrypointEncSlice` / `VAEntrypointEncSliceLP` (encodage) et, si `libcme` est présente, `VAProfileNone : VAEntrypointVideoProc`.

### Décodage avec FFmpeg

```bash
ffmpeg -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 \
       -i test_input.mp4 -f null -
```

### Encodage avec FFmpeg

```bash
ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 \
       -vf 'format=nv12,hwupload' -c:v h264_vaapi -b:v 5M output_h264.mp4

ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 \
       -vf 'format=nv12,hwupload' -c:v hevc_vaapi output_hevc.mp4
```

### Lecture avec mpv

```bash
mpv --hwdec=vaapi --vo=gpu test_input.mp4
```

L'export DMA-BUF prend en charge les modes `VA_EXPORT_SURFACE_COMPOSED_LAYERS` et `VA_EXPORT_SURFACE_SEPARATE_LAYERS` (utilisé par mpv, Kodi et `hwmap` de FFmpeg).

---

## 7. Diagnostic et débogage

### Journalisation (`CIX_VAAPI_LOG`)

| Niveau | Messages |
|---|---|
| `0` | erreurs |
| `1` | avertissements (défaut) |
| `2` | informations |
| `3` | débogage |

```bash
CIX_VAAPI_LOG=3 vainfo --display drm --device /dev/dri/renderD128
```

### Dumps

* Flux compressé envoyé à la VPU : `CIX_VAAPI_DUMP_BITS=1` → `cix-vaapi-dump-<date>-<pid>-<n>.bin`
* Trames : `CIX_VAAPI_DUMP_FRAME=1` → `cix-vaapi-dump-<date>-<pid>-<n>.yuv`

Les fichiers sont créés dans le répertoire courant.

### Problèmes fréquents

| Symptôme | Cause probable |
|---|---|
| `No valid V4L2 codec devices found` | pilote VPU non chargé ou utilisateur hors du groupe `video` |
| `Failed to allocate ... DMA-BUF` | `/dev/dma_heap/system` inaccessible (règle udev du paquet, nouvelle session) |
| HEVC/AV1 décodés en logiciel | libva standard : installer `cix-libva2` / `cix-libva-dev` puis recompiler |
| pas de `VAEntrypointVideoProc` | `libcme` absente lors de la compilation |

---

## 8. Limites connues

* `vaGetImage` ne gère que la copie directe d'une surface entière dans une image de même format ; `vaPutImage` n'est pas implémenté (FFmpeg et GStreamer utilisent `vaDeriveImage`).
* L'import de mémoire externe (DRM PRIME, pointeurs utilisateur) dans `vaCreateSurfaces` n'est pas pris en charge : il est désormais signalé par `VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE`.
* Pas de B-frames ni d'encodage 10 bits.
* H.264 : les flux utilisant `pic_order_cnt_type = 1` ne sont pas pris en charge (champs SPS absents de VA-API).

---

## 9. Licence

Ce projet est distribué sous licence **Apache License 2.0**. Consultez le fichier `LICENSE`.
