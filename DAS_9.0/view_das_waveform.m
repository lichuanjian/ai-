%% DAS binary waveform viewer
% File format: little-endian float32, packet layout [rows, cols] in C-order.

clear;
clc;

dataFile = ['C:\Users\admin\xwechat_files\wxid_wxf3v7jymejh22_11d2\msg\file\2026-09\' ...
    'data_all_2026_08_30_11_48_02_0_freq10000Hz_rows50_cols5000_' ...
    'interval0.8_pitch0.4_end40.0_start0.0_data.bin'];

sampleRate = 10000;
rows = 50;
colsPerPacket = 5000;
channelSpacing = 0.8;
channel1 = 1;
channel2 = 2;

assert(isfile(dataFile), 'Data file not found: %s', dataFile);

fileInfo = dir(dataFile);
assert(mod(fileInfo.bytes, 4) == 0, ...
    'File size must be a multiple of four bytes for float32 data.');

fileID = fopen(dataFile, 'r', 'ieee-le');
assert(fileID ~= -1, 'Unable to open data file: %s', dataFile);
cleanupFile = onCleanup(@() fclose(fileID));
raw = fread(fileID, Inf, 'single=>single');

valuesPerPacket = rows * colsPerPacket;
assert(mod(numel(raw), valuesPerPacket) == 0, ...
    ['Data length does not match rows=%d and cols/packet=%d. ' ...
     'float32 values=%d, remainder=%d.'], ...
    rows, colsPerPacket, numel(raw), mod(numel(raw), valuesPerPacket));

packetCount = numel(raw) / valuesPerPacket;
totalSamples = packetCount * colsPerPacket;
data = zeros(rows, totalSamples, 'single');

% Match NumPy reshape(packetCount, rows, colsPerPacket) in C-order,
% followed by transpose(1, 0, 2) and time concatenation.
for packetIndex = 1:packetCount
    firstValue = (packetIndex - 1) * valuesPerPacket + 1;
    lastValue = packetIndex * valuesPerPacket;
    packet = reshape(raw(firstValue:lastValue), colsPerPacket, rows).';
    firstColumn = (packetIndex - 1) * colsPerPacket + 1;
    lastColumn = packetIndex * colsPerPacket;
    data(:, firstColumn:lastColumn) = packet;
end

% Convert the raw numeric matrix to scaled phase values.
data = data / single(2^29);

time = (0:totalSamples - 1) / sampleRate;
distance = (0:rows - 1) * channelSpacing;

absoluteValues = sort(abs(double(data(:))));
limitIndex = max(1, round(0.99 * numel(absoluteValues)));
colorLimit = absoluteValues(limitIndex);
if ~isfinite(colorLimit) || colorLimit <= 0
    colorLimit = 1;
end

figure('Name', 'DAS Waveform Viewer', 'Color', 'w');
layout = tiledlayout(3, 1, 'TileSpacing', 'compact', 'Padding', 'compact');

axWaterfall = nexttile(layout, 1);
imagesc(axWaterfall, distance, time, data.');
axis(axWaterfall, 'xy');
xlabel(axWaterfall, 'Distance (m)');
ylabel(axWaterfall, 'Time (s)');
title(axWaterfall, sprintf( ...
    'DAS Waterfall | fs=%g Hz, rows=%d, cols/packet=%d, spacing=%g m', ...
    sampleRate, rows, colsPerPacket, channelSpacing));
clim(axWaterfall, [-colorLimit, colorLimit]);
colorBarHandle = colorbar(axWaterfall);
colorBarHandle.Label.String = 'Phase / rad';

colorSteps = 128;
blueToWhite = [linspace(0, 1, colorSteps).', ...
               linspace(0, 1, colorSteps).', ...
               ones(colorSteps, 1)];
whiteToRed = [ones(colorSteps, 1), ...
              linspace(1, 0, colorSteps).', ...
              linspace(1, 0, colorSteps).'];
colormap(axWaterfall, [blueToWhite; whiteToRed]);

axChannel1 = nexttile(layout, 2);
plot(axChannel1, time, data(channel1, :), 'LineWidth', 0.8);
grid(axChannel1, 'on');
xlabel(axChannel1, 'Time (s)');
ylabel(axChannel1, 'rad');
title(axChannel1, sprintf('Channel %d | Distance %g m', ...
    channel1, distance(channel1)));

axChannel2 = nexttile(layout, 3);
plot(axChannel2, time, data(channel2, :), 'LineWidth', 0.8);
grid(axChannel2, 'on');
xlabel(axChannel2, 'Time (s)');
ylabel(axChannel2, 'rad');
title(axChannel2, sprintf('Channel %d | Distance %g m', ...
    channel2, distance(channel2)));

linkaxes([axChannel1, axChannel2], 'x');

fprintf('File: %s\n', dataFile);
fprintf('Packets: %d\n', packetCount);
fprintf('Data shape: %d channels x %d samples\n', rows, totalSamples);
fprintf('Duration: %.6f s\n', totalSamples / sampleRate);
fprintf('Scale divisor: 2^29 = %g\n', 2^29);
fprintf('Scaled range: [%g, %g]\n', min(data(:)), max(data(:)));
