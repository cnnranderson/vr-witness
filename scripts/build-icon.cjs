// Regenerate the checked-in Windows icon after changing assets/launcher.svg.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const sharp = require(require.resolve('sharp', {
    paths: [root, path.join(root, '.tools', 'icon-tools')],
}));
const sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];

async function main() {
    const svg = fs.readFileSync(path.join(root, 'assets', 'launcher.svg'));
    const frames = await Promise.all(sizes.map(size =>
        sharp(svg).resize(size, size).png().toBuffer()));
    const directory = Buffer.alloc(6 + sizes.length * 16);
    directory.writeUInt16LE(1, 2);
    directory.writeUInt16LE(sizes.length, 4);
    let offset = directory.length;
    frames.forEach((frame, i) => {
        const entry = 6 + i * 16;
        // ICO stores 256 as zero and permits a PNG payload for each size.
        directory[entry] = directory[entry + 1] = sizes[i] % 256;
        directory.writeUInt16LE(1, entry + 4);
        directory.writeUInt16LE(32, entry + 6);
        directory.writeUInt32LE(frame.length, entry + 8);
        directory.writeUInt32LE(offset, entry + 12);
        offset += frame.length;
    });
    fs.writeFileSync(path.join(root, 'assets', 'launcher.ico'),
        Buffer.concat([directory, ...frames]));
    const preview = path.join(root, 'out', 'icon-review');
    fs.mkdirSync(preview, {recursive: true});
    await sharp(svg).png().toFile(path.join(preview, 'launcher-icon.png'));
    console.log('Generated launcher.ico (16-256 px) and the full-size preview.');
}
main().catch(error => {
    console.error(error.message);
    process.exitCode = 1;
});
