function plot_scope_fft()
%PLOT_SCOPE_FFT FFT analysis for oscilloscope CSV exports on E:.
%
% Run in MATLAB:
%   plot_scope_fft
%
% Outputs are saved to:
%   E:\fft_results

clc;
close all;

files = {
    findScopeCsv('380'), '380M center / AOM 4G sampling', 380, 375, 250, 500;
    findScopeCsv('400'), '400M center / AOM 4G sampling', 400, 390, 290, 490
};

outDir = 'E:\fft_results';
if ~exist(outDir, 'dir')
    mkdir(outDir);
end

timeFig = figure('Color', 'w', 'Name', 'Oscilloscope time domain');
tiledlayout(timeFig, 2, 1, 'TileSpacing', 'compact', 'Padding', 'compact');

fftFig = figure('Color', 'w', 'Name', 'Single-sided FFT spectrum');
hold on;
grid on;
box on;

zoomFig = figure('Color', 'w', 'Name', 'FFT spectrum zoom');
hold on;
grid on;
box on;

summary = struct([]);
for k = 1:size(files, 1)
    filePath = files{k, 1};
    label = files{k, 2};
    centerMHz = files{k, 3};
    hannCenterMHz = files{k, 4};
    plotMinMHz = files{k, 5};
    plotMaxMHz = files{k, 6};
    hannFullWidthHz = (plotMaxMHz - plotMinMHz) * 1e6;

    data = readScopeCsv(filePath);
    fs = 1 / data.tInc;
    n = numel(data.v);
    t = data.t0 + (0:n-1).' * data.tInc;

    [freqHz, ampVrms, ampDbv] = singleSidedFft(data.v, fs);

    [peakDbv, peakIdx] = max(ampDbv(2:end));
    peakIdx = peakIdx + 1;
    bandIdx = find(freqHz >= 300e6 & freqHz <= 500e6);
    [bandPeakDbv, bandRelIdx] = max(ampDbv(bandIdx));
    bandPeakIdx = bandIdx(bandRelIdx);
    centerIdx = find(abs(freqHz - hannCenterMHz * 1e6) <= 5e6);
    [centerPeakDbv, centerRelIdx] = max(ampDbv(centerIdx));
    centerPeakIdx = centerIdx(centerRelIdx);

    summary(k).file = filePath; %#ok<AGROW>
    summary(k).label = label; %#ok<AGROW>
    summary(k).center_MHz = centerMHz; %#ok<AGROW>
    summary(k).hannCenter_MHz = hannCenterMHz; %#ok<AGROW>
    summary(k).plotMin_MHz = plotMinMHz; %#ok<AGROW>
    summary(k).plotMax_MHz = plotMaxMHz; %#ok<AGROW>
    summary(k).samples = n; %#ok<AGROW>
    summary(k).fs_Hz = fs; %#ok<AGROW>
    summary(k).duration_s = n / fs; %#ok<AGROW>
    summary(k).overallPeakFreq_Hz = freqHz(peakIdx); %#ok<AGROW>
    summary(k).overallPeakAmp_Vrms = ampVrms(peakIdx); %#ok<AGROW>
    summary(k).overallPeakAmp_dBV = peakDbv; %#ok<AGROW>
    summary(k).band300_500PeakFreq_Hz = freqHz(bandPeakIdx); %#ok<AGROW>
    summary(k).band300_500PeakAmp_dBV = bandPeakDbv; %#ok<AGROW>
    summary(k).hannCenterPm5MHzPeakFreq_Hz = freqHz(centerPeakIdx); %#ok<AGROW>
    summary(k).hannCenterPm5MHzPeakAmp_dBV = centerPeakDbv; %#ok<AGROW>

    figure(timeFig);
    nexttile;
    pointsToShow = min(n, round(1e-6 * fs)); % first 1 us
    plot(t(1:pointsToShow) * 1e6, data.v(1:pointsToShow), 'LineWidth', 1);
    grid on;
    box on;
    xlabel('Time (\mus)');
    ylabel('Voltage (V)');
    title(label, 'Interpreter', 'none');

    figure(fftFig);
    plot(freqHz / 1e6, ampDbv, 'LineWidth', 1.1, 'DisplayName', label);

    figure(zoomFig);
    plot(freqHz / 1e6, ampDbv, 'LineWidth', 1.1, 'DisplayName', label);

    [~, baseName, ~] = fileparts(filePath);

    singleFig = figure('Color', 'w', 'Name', ['FFT - ' label]);
    plot(freqHz / 1e6, ampDbv, 'LineWidth', 1.1);
    grid on;
    box on;
    xlabel('Frequency (MHz)');
    ylabel('Amplitude (dBV rms)');
    title(['Single-sided FFT spectrum - ' label], 'Interpreter', 'none');
    xlim([0, min(2000, max(freqHz / 1e6))]);
    saveas(singleFig, fullfile(outDir, [baseName '_fft_full.png']));
    savefig(singleFig, fullfile(outDir, [baseName '_fft_full.fig']));

    singleZoomFig = figure('Color', 'w', 'Name', ['FFT with Hann - ' label]);
    plot(freqHz / 1e6, ampDbv, 'LineWidth', 1.1, 'DisplayName', 'FFT');
    hold on;
    hannDbv = centeredHannDbv(freqHz, hannCenterMHz * 1e6, hannFullWidthHz, centerPeakDbv);
    plot(freqHz / 1e6, hannDbv, 'r--', 'LineWidth', 1.4, ...
        'DisplayName', sprintf('Hann window, center %.0f MHz', hannCenterMHz));
    plot(freqHz(centerPeakIdx) / 1e6, centerPeakDbv, 'ko', ...
        'MarkerFaceColor', 'k', 'DisplayName', 'FFT peak near center');
    grid on;
    box on;
    xlabel('Frequency (MHz)');
    ylabel('Amplitude (dBV rms)');
    title(sprintf('FFT vs Hann window, %.0f MHz center', hannCenterMHz), 'Interpreter', 'none');
    legend('Location', 'best', 'Interpreter', 'none');
    xlim([plotMinMHz, plotMaxMHz]);
    ylim([max(-140, centerPeakDbv - 90), centerPeakDbv + 10]);
    rangeTag = sprintf('%.0f_%.0fMHz', plotMinMHz, plotMaxMHz);
    saveas(singleZoomFig, fullfile(outDir, [baseName '_fft_hann_' rangeTag '.png']));
    savefig(singleZoomFig, fullfile(outDir, [baseName '_fft_hann_' rangeTag '.fig']));

    csvOut = fullfile(outDir, [baseName '_fft_spectrum.csv']);
    spectrumTable = table(freqHz, ampVrms, ampDbv, ...
        'VariableNames', {'Frequency_Hz', 'Amplitude_Vrms', 'Amplitude_dBV'});
    writetable(spectrumTable, csvOut);
end

figure(fftFig);
xlabel('Frequency (MHz)');
ylabel('Amplitude (dBV rms)');
title('Single-sided FFT spectrum');
legend('Location', 'best', 'Interpreter', 'none');
xlim([0, min(2000, max(freqHz / 1e6))]);

figure(zoomFig);
xlabel('Frequency (MHz)');
ylabel('Amplitude (dBV rms)');
title('FFT spectrum, 300-500 MHz zoom');
legend('Location', 'best', 'Interpreter', 'none');
xlim([300, 500]);

saveas(timeFig, fullfile(outDir, 'scope_time_domain.png'));
savefig(timeFig, fullfile(outDir, 'scope_time_domain.fig'));
saveas(fftFig, fullfile(outDir, 'scope_fft_spectrum.png'));
savefig(fftFig, fullfile(outDir, 'scope_fft_spectrum.fig'));
saveas(zoomFig, fullfile(outDir, 'scope_fft_spectrum_300_500MHz.png'));
savefig(zoomFig, fullfile(outDir, 'scope_fft_spectrum_300_500MHz.fig'));

summaryTable = struct2table(summary);
writetable(summaryTable, fullfile(outDir, 'fft_summary.csv'));

disp(summaryTable);
fprintf('\nSaved results to: %s\n', outDir);

end

function filePath = findScopeCsv(centerMHzText)
matches = dir(fullfile('E:\', '*.csv'));
names = {matches.name};
isTarget = contains(names, centerMHzText, 'IgnoreCase', true) & ...
    contains(names, 'AOM4G', 'IgnoreCase', true);
idx = find(isTarget, 1, 'first');
assert(~isempty(idx), 'Cannot find %sM AOM4G CSV under E:\', centerMHzText);
filePath = fullfile(matches(idx).folder, matches(idx).name);
end

function data = readScopeCsv(filePath)
fid = fopen(filePath, 'r');
assert(fid > 0, 'Cannot open file: %s', filePath);
cleaner = onCleanup(@() fclose(fid));

header = fgetl(fid);
t0 = readHeaderValue(header, 't0');
tInc = readHeaderValue(header, 'tInc');

values = textscan(fid, '%f%*[^\n]', 'Delimiter', ',', 'CollectOutput', true);
v = values{1};
v = v(isfinite(v));

data = struct('t0', t0, 'tInc', tInc, 'v', v);
end

function value = readHeaderValue(header, key)
expr = [key '\s*=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)'];
token = regexp(header, expr, 'tokens', 'once');
assert(~isempty(token), 'Cannot find %s in header: %s', key, header);
value = str2double(token{1});
end

function [freqHz, ampVrms, ampDbv] = singleSidedFft(v, fs)
v = v(:);
v = v - mean(v, 'omitnan');
n = numel(v);

if exist('hann', 'file') == 2
    win = hann(n, 'periodic');
else
    win = 0.5 - 0.5 * cos(2*pi*(0:n-1).' / n);
end

coherentGain = mean(win);
y = fft(v .* win);
nOneSided = floor(n / 2) + 1;
y = y(1:nOneSided);

ampPeak = abs(y) / (n * coherentGain);
if nOneSided > 2
    ampPeak(2:end-1) = 2 * ampPeak(2:end-1);
end
ampVrms = ampPeak / sqrt(2);
ampDbv = 20 * log10(max(ampVrms, eps));

freqHz = (0:nOneSided-1).' * fs / n;
end

function hannDbv = centeredHannDbv(freqHz, centerHz, fullWidthHz, peakDbv)
hannDbv = nan(size(freqHz));
halfWidthHz = fullWidthHz / 2;
inWindow = abs(freqHz - centerHz) <= halfWidthHz;
x = (freqHz(inWindow) - centerHz) / halfWidthHz;
win = 0.5 * (1 + cos(pi * x));
hannDbv(inWindow) = peakDbv + 20 * log10(max(win, eps));
end
