import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

import { DataUtils } from 'three';
import { EXRLoader } from 'three/addons/loaders/EXRLoader.js';

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const webDirectory = dirname(scriptDirectory);
const sourceDirectory = join(webDirectory, 'assets', 'exr');
const outputDirectory = join(webDirectory, 'src', 'assets', 'gallery');
const targetWidth = 1200;

const files = [
  'chess.exr',
  'anisotropic lamp.exr',
  'helmet.exr',
  'sponza.exr',
];

function clamp(value, minimum = 0, maximum = 1) {
  return Math.min(Math.max(value, minimum), maximum);
}

function acesFilmic(value) {
  const exposed = Math.max(value, 0);
  return clamp(
    (exposed * ((2.51 * exposed) + 0.03))
      / (exposed * ((2.43 * exposed) + 0.59) + 0.14),
  );
}

function linearToSrgb(value) {
  return value <= 0.0031308
    ? value * 12.92
    : (1.055 * (value ** (1 / 2.4))) - 0.055;
}

function toByte(half) {
  return Math.round(clamp(linearToSrgb(acesFilmic(DataUtils.fromHalfFloat(half)))) * 255);
}

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const value of buffer) {
    crc ^= value;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBuffer = Buffer.from(type, 'ascii');
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const checksum = Buffer.alloc(4);
  checksum.writeUInt32BE(crc32(Buffer.concat([typeBuffer, data])));
  return Buffer.concat([length, typeBuffer, data, checksum]);
}

function encodePng(width, height, rgba) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 6;

  const scanlines = Buffer.alloc(height * ((width * 4) + 1));
  for (let y = 0; y < height; y += 1) {
    const destination = y * ((width * 4) + 1);
    scanlines[destination] = 0;
    rgba.copy(scanlines, destination + 1, y * width * 4, (y + 1) * width * 4);
  }

  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    pngChunk('IHDR', header),
    pngChunk('IDAT', deflateSync(scanlines, { level: 9 })),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

function posterName(filename) {
  return `${filename.slice(0, -extname(filename).length).replaceAll(' ', '-')}.png`;
}

function generatePoster(filename) {
  const source = readFileSync(join(sourceDirectory, filename));
  const arrayBuffer = source.buffer.slice(source.byteOffset, source.byteOffset + source.byteLength);
  const image = new EXRLoader().parse(arrayBuffer);
  const targetHeight = Math.round((image.height / image.width) * targetWidth);
  const rgba = Buffer.alloc(targetWidth * targetHeight * 4);

  for (let y = 0; y < targetHeight; y += 1) {
    const sourceY = image.height - 1
      - Math.min(Math.floor((y / targetHeight) * image.height), image.height - 1);
    for (let x = 0; x < targetWidth; x += 1) {
      const sourceX = Math.min(Math.floor((x / targetWidth) * image.width), image.width - 1);
      const sourceOffset = ((sourceY * image.width) + sourceX) * 4;
      const destinationOffset = ((y * targetWidth) + x) * 4;
      rgba[destinationOffset] = toByte(image.data[sourceOffset]);
      rgba[destinationOffset + 1] = toByte(image.data[sourceOffset + 1]);
      rgba[destinationOffset + 2] = toByte(image.data[sourceOffset + 2]);
      rgba[destinationOffset + 3] = 255;
    }
  }

  const outputPath = join(outputDirectory, posterName(filename));
  writeFileSync(outputPath, encodePng(targetWidth, targetHeight, rgba));
  console.log(`${filename} -> ${outputPath}`);
}

mkdirSync(outputDirectory, { recursive: true });
files.forEach(generatePoster);
