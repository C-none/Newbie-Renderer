import anisotropicLampExrUrl from '../../assets/exr/anisotropic lamp.exr';
import chessExrUrl from '../../assets/exr/chess.exr';
import helmetExrUrl from '../../assets/exr/helmet.exr';
import sponzaExrUrl from '../../assets/exr/sponza.exr';
import anisotropicLampPosterUrl from '../assets/gallery/anisotropic-lamp.png';
import chessPosterUrl from '../assets/gallery/chess.png';
import helmetPosterUrl from '../assets/gallery/helmet.png';
import sponzaPosterUrl from '../assets/gallery/sponza.png';

export const galleryItems = [
  {
    name: 'chess.exr',
    exrUrl: chessExrUrl,
    posterUrl: chessPosterUrl,
    size: '6.6 MB',
    resolution: '1920 × 1080',
    title: 'Chess',
    description: 'Transmission and volume across glass, stone, and metal materials.',
    tags: ['Transmission', 'Volume', 'Metal'],
    credit: 'A Beautiful Game — MaterialX Project and Ed Mackey, CC BY 4.0',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ABeautifulGame',
  },
  {
    name: 'anisotropic lamp.exr',
    exrUrl: anisotropicLampExrUrl,
    posterUrl: anisotropicLampPosterUrl,
    size: '8.7 MB',
    resolution: '2560 × 1600',
    title: 'Anisotropic Lamp',
    description: 'Brushed metal with anisotropic material response and directional highlights.',
    tags: ['Anisotropy', 'Metal', 'Environment'],
    credit: 'Anisotropy Barn Lamp — Wayfair and Eric Chadwick, CC BY 4.0',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AnisotropyBarnLamp',
  },
  {
    name: 'helmet.exr',
    exrUrl: helmetExrUrl,
    posterUrl: helmetPosterUrl,
    size: '16.4 MB',
    resolution: '2560 × 1600',
    title: 'Damaged Helmet',
    description: 'Metallic-roughness PBR, normal mapping, and image-based environment lighting.',
    tags: ['Metallic-Roughness', 'Normal Map', 'IBL'],
    credit: 'Damaged Helmet — theblueturtle_ and ctxwing, CC BY-NC 4.0 and CC BY 4.0',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet',
  },
  {
    name: 'sponza.exr',
    exrUrl: sponzaExrUrl,
    posterUrl: sponzaPosterUrl,
    size: '16.4 MB',
    resolution: '2560 × 1600',
    title: 'Sponza',
    description: 'Path-traced textured architecture with alpha-masked geometry.',
    tags: ['Architecture', 'Alpha Mask', 'Textures'],
    credit: 'Sponza — Crytek, CryEngine Limited License Agreement',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza',
  },
];

export const galleryMetadata = {
  renderer: 'Newbie Renderer',
  pipeline: 'rtobject path tracing',
  sampling: 'Current pipeline contract: 1 spp per frame',
  format: 'Linear HDR · OpenEXR',
  inspector: 'Decoded and tone-mapped client-side with three.js',
  provenance: 'Exact capture GPU, driver, commit, DLSS mode, and timing were not embedded in these published EXR files.',
};
