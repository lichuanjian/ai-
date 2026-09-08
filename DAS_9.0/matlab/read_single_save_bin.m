function result = read_single_save_bin(folderPath, varargin)
%READ_SINGLE_SAVE_BIN Read DAS single-point saved bin files and apply 1 Hz high-pass.
%
%   result = READ_SINGLE_SAVE_BIN(folderPath)
%   result = READ_SINGLE_SAVE_BIN(folderPath, 'BinIndex', 1)
%   result = READ_SINGLE_SAVE_BIN(folderPath, 'HighpassHz', 1, 'SaveMat', true)
%
% The Qt saver writes single-point data as raw little-endian float32 values.
% Each save call appends one channel row with COLS samples. Folder name carries
% metadata such as freq, rows, cols, pitch, interval, startM and endM.
%
% Filtering uses a causal IIR high-pass by default. This avoids the large
% start/end ringing that can happen when zero-phase filters extrapolate data
% at the two edges.

opts = parseInputs(folderPath, varargin{:});
meta = parseSingleSaveFolder(opts.folderPath);

if ~meta.valid
    error('Cannot parse valid metadata from folder name: %s', meta.folderName);
end
if meta.isAllData
    error(['This folder starts with data_all_ and is full-line data. ', ...
           'Please choose a single-point folder starting with data_.']);
end
if meta.binCount == 0
    error('No .bin files found in folder: %s', meta.folderPath);
end
if meta.frequency <= 2 * opts.highpassHz
    error('Sampling frequency %.3f Hz is too low for %.3f Hz high-pass.', ...
          meta.frequency, opts.highpassHz);
end

selectedBins = selectBins(meta, opts.binIndex);
[totalSamples, frameCountByFile] = countSamples(meta, selectedBins);
estimatedRawGB = double(totalSamples) * 4 / 1024^3;
if estimatedRawGB > opts.maxLoadGB
    error(['Selected data is %.2f GB as float32, exceeding MaxLoadGB=%.2f. ', ...
           'Read one BinIndex at a time or increase MaxLoadGB if your MATLAB memory is enough.'], ...
          estimatedRawGB, opts.maxLoadGB);
end

raw = readSelectedBins(meta, selectedBins, totalSamples);
[filtered, filterInfo] = highpassOneHz(raw, meta.frequency, opts.highpassHz, opts.filterOrder, opts.useZeroPhase);

time = (0:numel(raw)-1).' / meta.frequency;
result = struct();
result.time = time;
result.raw = raw;
result.filtered = filtered;
result.meta = meta;
result.selectedBinFiles = selectedBins;
result.frameCountByFile = frameCountByFile;
result.totalSamples = totalSamples;
result.durationSeconds = double(totalSamples) / meta.frequency;
result.filter = filterInfo;

if opts.makePlot
    result.plot = plotSinglePointResult(result, opts.maxPlotPoints);
end

if opts.saveMat
    matPath = opts.outputMatPath;
    if isempty(matPath)
        matPath = fullfile(meta.folderPath, 'single_point_1Hz_highpass.mat');
    end
    save(matPath, 'result', '-v7.3');
    result.savedMatPath = matPath;
end

printSummary(result);
end

function opts = parseInputs(folderPath, varargin)
if nargin < 1 || isempty(folderPath)
    picked = uigetdir(pwd, 'Select DAS single-point saved folder');
    if isequal(picked, 0)
        error('No folder selected.');
    end
    folderPath = picked;
end

if ~ischar(folderPath)
    error('folderPath must be a character path, for example: ''E:\data\data_...''');
end

opts = struct();
opts.folderPath = folderPath;
opts.BinIndex = [];
opts.HighpassHz = 1;
opts.FilterOrder = 2;
opts.UseZeroPhase = false;
opts.MaxLoadGB = 8;
opts.MaxPlotPoints = 250000;
opts.MakePlot = true;
opts.SaveMat = false;
opts.OutputMatPath = '';

if mod(numel(varargin), 2) ~= 0
    error('Optional arguments must be name/value pairs, for example: ''BinIndex'', 1.');
end

for k = 1:2:numel(varargin)
    name = varargin{k};
    value = varargin{k + 1};
    if ~ischar(name)
        error('Option name must be a character string.');
    end
    switch lower(name)
        case 'binindex'
            opts.BinIndex = value;
        case 'highpasshz'
            opts.HighpassHz = value;
        case 'filterorder'
            opts.FilterOrder = value;
        case 'usezerophase'
            opts.UseZeroPhase = value;
        case 'maxloadgb'
            opts.MaxLoadGB = value;
        case 'maxplotpoints'
            opts.MaxPlotPoints = value;
        case 'makeplot'
            opts.MakePlot = value;
        case 'savemat'
            opts.SaveMat = value;
        case 'outputmatpath'
            opts.OutputMatPath = value;
        otherwise
            error('Unknown option: %s', name);
    end
end

opts.folderPath = regexprep(strtrim(opts.folderPath), '[\\/]+$', '');
opts.highpassHz = double(opts.HighpassHz);
opts.filterOrder = double(opts.FilterOrder);
opts.useZeroPhase = logical(opts.UseZeroPhase);
opts.maxLoadGB = double(opts.MaxLoadGB);
opts.maxPlotPoints = double(opts.MaxPlotPoints);
opts.makePlot = logical(opts.MakePlot);
opts.saveMat = logical(opts.SaveMat);
opts.outputMatPath = char(opts.OutputMatPath);
opts.binIndex = opts.BinIndex;

if exist(opts.folderPath, 'dir') ~= 7
    error('Folder does not exist: %s', opts.folderPath);
end

if ~(isnumeric(opts.highpassHz) && isscalar(opts.highpassHz) && opts.highpassHz >= 0)
    error('HighpassHz must be a scalar number >= 0.');
end
if ~(isnumeric(opts.filterOrder) && isscalar(opts.filterOrder) && opts.filterOrder >= 1)
    error('FilterOrder must be a scalar number >= 1.');
end
if ~(isnumeric(opts.maxLoadGB) && isscalar(opts.maxLoadGB) && opts.maxLoadGB > 0)
    error('MaxLoadGB must be a scalar number > 0.');
end
if ~(isnumeric(opts.maxPlotPoints) && isscalar(opts.maxPlotPoints) && opts.maxPlotPoints >= 1000)
    error('MaxPlotPoints must be a scalar number >= 1000.');
end
if ~isempty(opts.binIndex) && ~isnumeric(opts.binIndex)
    error('BinIndex must be numeric, for example 1 or [1 2].');
end
end

function meta = parseSingleSaveFolder(folderPath)
[~, folderName] = fileparts(folderPath);

meta = struct();
meta.valid = false;
meta.folderPath = folderPath;
meta.folderName = folderName;
meta.isAllData = false;
meta.timestamp = '';
meta.frequency = NaN;
meta.extractCount = NaN;
meta.differentialDistance = NaN;
meta.startChannel = NaN;
meta.endChannel = NaN;
meta.rows = NaN;
meta.cols = NaN;
meta.pitchMeters = NaN;
meta.intervalMeters = NaN;
meta.startMeters = NaN;
meta.endMeters = NaN;
meta.binFiles = struct('name', {}, 'folder', {}, 'date', {}, 'bytes', {}, 'isdir', {}, 'datenum', {});
meta.binCount = 0;

fullPattern = ['^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})', ...
    '_freq(\d+)Hz_ext(\d+)_diff(\d+)_startCh(\d+)_endCh(\d+)', ...
    '_rows(\d+)_cols(\d+)_pitch([-+]?\d+(?:\.\d+)?)m_interval([-+]?\d+(?:\.\d+)?)m', ...
    '_startM([-+]?\d+(?:\.\d+)?)_endM([-+]?\d+(?:\.\d+)?)$'];
tokens = regexp(folderName, fullPattern, 'tokens', 'once');

if ~isempty(tokens)
    meta.isAllData = strcmp(tokens{1}, 'data_all_');
    meta.timestamp = tokens{2};
    meta.frequency = str2double(tokens{3});
    meta.extractCount = str2double(tokens{4});
    meta.differentialDistance = str2double(tokens{5});
    meta.startChannel = str2double(tokens{6});
    meta.endChannel = str2double(tokens{7});
    meta.rows = str2double(tokens{8});
    meta.cols = str2double(tokens{9});
    meta.pitchMeters = str2double(tokens{10});
    meta.intervalMeters = str2double(tokens{11});
    meta.startMeters = str2double(tokens{12});
    meta.endMeters = str2double(tokens{13});
else
    compactPattern = ['^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})', ...
        '_freq(\d+)Hz_rows(\d+)_cols(\d+)', ...
        '_pitch([-+]?\d+(?:\.\d+)?)_end([-+]?\d+(?:\.\d+)?)', ...
        '_start([-+]?\d+(?:\.\d+)?)_interval([-+]?\d+(?:\.\d+)?)$'];
    tokens = regexp(folderName, compactPattern, 'tokens', 'once');
    if ~isempty(tokens)
        meta.isAllData = strcmp(tokens{1}, 'data_all_');
        meta.timestamp = tokens{2};
        meta.frequency = str2double(tokens{3});
        meta.rows = str2double(tokens{4});
        meta.cols = str2double(tokens{5});
        meta.pitchMeters = str2double(tokens{6});
        meta.endMeters = str2double(tokens{7});
        meta.startMeters = str2double(tokens{8});
        meta.intervalMeters = str2double(tokens{9});
        meterPerRawPoint = meterPerRawPointForFrequency(meta.frequency);
        meta.extractCount = max(1, round(meta.intervalMeters / meterPerRawPoint));
        meta.differentialDistance = max(1, round(meta.pitchMeters / meterPerRawPoint));
        meta.startChannel = max(0, round(meta.startMeters / meta.intervalMeters));
        meta.endChannel = max(meta.startChannel, round(meta.endMeters / meta.intervalMeters));
    end
end

meta.valid = isfinite(meta.frequency) && meta.frequency > 0 && ...
             isfinite(meta.rows) && meta.rows > 0 && ...
             isfinite(meta.cols) && meta.cols > 0;

binFiles = dir(fullfile(folderPath, '*.bin'));
meta.binFiles = sortBinFilesByIndex(binFiles);
meta.binCount = numel(meta.binFiles);
end

function meterPerRawPoint = meterPerRawPointForFrequency(frequency)
if frequency == 3333
    meterPerRawPoint = 1.2;
elseif frequency == 2000
    meterPerRawPoint = 2.0;
else
    meterPerRawPoint = 0.4;
end
end

function sortedFiles = sortBinFilesByIndex(binFiles)
if isempty(binFiles)
    sortedFiles = binFiles;
    return;
end
idx = zeros(numel(binFiles), 1);
for k = 1:numel(binFiles)
    token = regexp(binFiles(k).name, '_(\d+)\.bin$', 'tokens', 'once');
    if isempty(token)
        idx(k) = k - 1;
    else
        idx(k) = str2double(token{1});
    end
end
[~, order] = sort(idx);
sortedFiles = binFiles(order);
end

function selectedBins = selectBins(meta, binIndex)
if isempty(binIndex)
    selectedBins = meta.binFiles;
    return;
end
binIndex = unique(binIndex(:).');
if any(binIndex < 1) || any(binIndex > meta.binCount)
    error('BinIndex out of range. This folder has %d bin file(s).', meta.binCount);
end
selectedBins = meta.binFiles(binIndex);
end

function [totalSamples, frameCountByFile] = countSamples(meta, selectedBins)
bytesPerFloat = 4;
rowBytes = double(meta.cols) * bytesPerFloat;
totalSamples = 0;
frameCountByFile = zeros(numel(selectedBins), 1);

for k = 1:numel(selectedBins)
    fileBytes = double(selectedBins(k).bytes);
    if mod(fileBytes, bytesPerFloat) ~= 0
        error('File is not float32 aligned: %s', selectedBins(k).name);
    end
    if mod(fileBytes, rowBytes) ~= 0
        error(['File size does not match single-point frame length cols=%d: %s. ', ...
               'Expected bytes to be a multiple of cols*4.'], meta.cols, selectedBins(k).name);
    end
    frameCountByFile(k) = fileBytes / rowBytes;
    totalSamples = totalSamples + fileBytes / bytesPerFloat;
end
totalSamples = round(totalSamples);
end

function raw = readSelectedBins(meta, selectedBins, totalSamples)
raw = zeros(totalSamples, 1, 'single');
writeStart = 1;
for k = 1:numel(selectedBins)
    binPath = fullfile(meta.folderPath, selectedBins(k).name);
    fid = fopen(binPath, 'rb', 'ieee-le');
    if fid == -1
        error('Cannot open bin file: %s', binPath);
    end
    cleaner = onCleanup(@() fclose(fid)); %#ok<NASGU>
    data = fread(fid, inf, '*single');
    writeEnd = writeStart + numel(data) - 1;
    raw(writeStart:writeEnd) = data;
    writeStart = writeEnd + 1;
end
end

function [filtered, info] = highpassOneHz(raw, fs, cutoffHz, order, useZeroPhase)
info = struct();
info.cutoffHz = cutoffHz;
info.sampleRateHz = fs;
info.order = order;
info.type = 'IIR high-pass';
info.zeroPhase = false;

if cutoffHz <= 0
    filtered = raw;
    info.method = 'disabled';
    return;
end

x = double(raw(:));
bad = ~isfinite(x);
if any(bad)
    good = x(~bad);
    if isempty(good)
        x(bad) = 0;
    else
        x(bad) = mean(good);
    end
end

if exist('butter', 'file') == 2
    wn = cutoffHz / (fs / 2);
    [b, a] = butter(order, wn, 'high');
    if useZeroPhase && exist('filtfilt', 'file') == 2
        y = filtfilt(b, a, x);
        info.zeroPhase = true;
        info.method = sprintf('%d-order Butterworth IIR, filtfilt zero-phase', order);
        info.edgeHandling = 'zero-phase filtering; may ring at start/end if edge has jumps';
    else
        zi = steadyStateIirInitialCondition(b, a);
        y = filter(b, a, x, zi * x(1));
        info.method = sprintf('%d-order Butterworth IIR, causal', order);
        info.edgeHandling = 'steady-state initial condition, no reverse-edge extrapolation';
    end
else
    y = firstOrderIirHighpass(x, fs, cutoffHz);
    info.order = 1;
    info.method = '1-order RC IIR fallback';
    info.edgeHandling = 'causal filter initialized from first sample';
    info.note = 'Install Signal Processing Toolbox to use Butterworth IIR.';
end

filtered = single(y);
end

function zi = steadyStateIirInitialCondition(b, a)
% Equivalent to scipy.signal.lfilter_zi for MATLAB filter direct-form II.
a = a(:);
b = b(:);
if a(1) ~= 1
    b = b / a(1);
    a = a / a(1);
end

nfilt = max(numel(a), numel(b));
if numel(a) < nfilt
    a(numel(a)+1:nfilt, 1) = 0;
end
if numel(b) < nfilt
    b(numel(b)+1:nfilt, 1) = 0;
end

if nfilt == 1
    zi = zeros(0, 1);
    return;
end

A = [-a(2:end), [eye(nfilt - 2); zeros(1, nfilt - 2)]];
B = b(2:end) - b(1) * a(2:end);
zi = (eye(nfilt - 1) - A) \ B;
end

function y = firstOrderIirHighpass(x, fs, cutoffHz)
dt = 1 / fs;
tau = 1 / (2 * pi * cutoffHz);
alpha = tau / (tau + dt);
y = zeros(size(x));
y(1) = 0;
for n = 2:numel(x)
    y(n) = alpha * (y(n - 1) + x(n) - x(n - 1));
end
end

function plotInfo = plotSinglePointResult(result, maxPlotPoints)
n = result.totalSamples;
step = max(1, ceil(double(n) / maxPlotPoints));
idx = 1:step:n;
if idx(end) ~= n
    idx(end + 1) = n; %#ok<AGROW>
end

figure('Color', 'w', 'Name', sprintf('DAS single-point data: raw and %.3g Hz IIR high-pass', result.filter.cutoffHz));
t = result.time(idx);
subplot(2, 1, 1);
plot(t, result.raw(idx), 'Color', [0.20 0.39 0.82], 'LineWidth', 0.8);
grid on;
xlabel('Time / s');
ylabel('Raw phase');
title('Single-point raw waveform');

subplot(2, 1, 2);
plot(t, result.filtered(idx), 'Color', [0.88 0.28 0.15], 'LineWidth', 0.8);
grid on;
xlabel('Time / s');
ylabel('Filtered phase');
title(sprintf('%.3g Hz IIR high-pass waveform (%s)', result.filter.cutoffHz, result.filter.method));
legend(sprintf('%.3g Hz IIR high-pass', result.filter.cutoffHz), 'Location', 'best');
text(0.01, 0.94, sprintf('High-pass cutoff = %.3g Hz', result.filter.cutoffHz), ...
     'Units', 'normalized', 'Color', [0.55 0.18 0.10], 'FontWeight', 'bold');

plotInfo = struct();
plotInfo.step = step;
plotInfo.sampleCount = numel(idx);
end

function printSummary(result)
meta = result.meta;
fprintf('\nDAS single-point folder:\n  %s\n', meta.folderPath);
fprintf('Parsed metadata:\n');
fprintf('  frequency       = %.0f Hz\n', meta.frequency);
fprintf('  rows            = %d\n', meta.rows);
fprintf('  cols            = %d samples per saved frame\n', meta.cols);
fprintf('  extractCount    = %d\n', meta.extractCount);
fprintf('  diffDistance    = %d\n', meta.differentialDistance);
fprintf('  intervalMeters  = %.3f m\n', meta.intervalMeters);
fprintf('  pitchMeters     = %.3f m\n', meta.pitchMeters);
fprintf('Read result:\n');
fprintf('  bin files       = %d\n', numel(result.selectedBinFiles));
fprintf('  total samples   = %d\n', result.totalSamples);
fprintf('  duration        = %.6f s\n', result.durationSeconds);
fprintf('  high-pass       = %.3f Hz, method: %s\n\n', ...
        result.filter.cutoffHz, result.filter.method);
end
