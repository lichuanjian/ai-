function [data, meta] = read_data_all_bin(filePath, storageOrder)
%READ_DATA_ALL_BIN Read the DAS data_all binary format.
%   DATA = READ_DATA_ALL_BIN(FILEPATH) reads little-endian float32 data
%   stored in row-major order and returns DATA as [50, 5000, NFRAMES].
%
%   DATA = READ_DATA_ALL_BIN(FILEPATH, "column-major") can be used if the
%   producer is later confirmed to have written MATLAB column-major data.

if nargin < 2 || isempty(storageOrder)
    storageOrder = "row-major";
end

rows = 50;
cols = 5000;
sampleRateHz = 10000;
spatialStart = 0.0;
spatialInterval = 0.8;
bytesPerValue = 4; % IEEE-754 single

fileInfo = dir(filePath);
assert(~isempty(fileInfo), "File not found: %s", filePath);

bytesPerFrame = rows * cols * bytesPerValue;
assert(mod(fileInfo.bytes, bytesPerFrame) == 0, ...
    "File size %d is not a multiple of one 50x5000 float32 frame (%d bytes).", ...
    fileInfo.bytes, bytesPerFrame);

fid = fopen(filePath, "r", "ieee-le");
assert(fid >= 0, "Unable to open file: %s", filePath);
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

raw = fread(fid, Inf, "single=>single");
frameCount = numel(raw) / (rows * cols);
assert(frameCount == floor(frameCount), "The decoded value count is incomplete.");

switch lower(string(storageOrder))
    case "row-major"
        % C/Python layout: 5000 consecutive time samples for each row.
        data = permute(reshape(raw, [cols, rows, frameCount]), [2, 1, 3]);
    case "column-major"
        % MATLAB/Fortran layout: 50 consecutive rows for each column.
        data = reshape(raw, [rows, cols, frameCount]);
    otherwise
        error("storageOrder must be 'row-major' or 'column-major'.");
end

meta = struct();
meta.filePath = filePath;
meta.storageOrder = char(storageOrder);
meta.dataType = "single";
meta.endian = "little-endian";
meta.rows = rows;
meta.cols = cols;
meta.frames = frameCount;
meta.sampleRateHz = sampleRateHz;
meta.timeSeconds = (0:cols-1) / sampleRateHz;
meta.spatialPosition = spatialStart + (0:rows-1) * spatialInterval;

fprintf("Read %d frames from %s\n", frameCount, filePath);
fprintf("Data size: %d x %d x %d, type: %s, order: %s\n", ...
    size(data, 1), size(data, 2), size(data, 3), class(data), storageOrder);
fprintf("Range: %.9g to %.9g; NaN: %d; Inf: %d\n", ...
    min(data, [], "all"), max(data, [], "all"), ...
    sum(isnan(data), "all"), sum(isinf(data), "all"));
end
