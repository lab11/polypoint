% modified S-V channel model evaluation
%
% Written by Sun Xu, Kim Chee Wee, B. Kannan & Francois Chin on 14/09/2004
clear;
no_output_files = 1; % non-zero: avoids writing output files of continuous-time responses
num_channels = 100; % number of channel impulse responses to generate
%randn('state',12); % initialize state of function for repeatability
%rand('state',12); % initialize state of function for repeatability
cm_num = 1; % channel model number from 1 to 8
% get channel model params based on this channel model number
[Lam,lambda,Lmean,lambda_mode,lambda_1,lambda_2,beta,Gam,gamma_0,Kgamma, ...
sigma_cluster,nlos,gamma_rise,gamma_1,chi,m0,Km,sigma_m0,sigma_Km, ...
sfading_mode,m0_sp,std_shdw,kappa,fc,fs] = uwb_sv_params_15_4a( cm_num );
fprintf(1,['Model Parameters\n' ...
' Lam = %.4f, lambda = %.4f, Lmean = %.4f, lambda_mode(FLAG) = %d\n' ...
' lambda_1 = %.4f, lambda_2 = %.4f, beta = %.4f\n' ...
' Gam = %.4f, gamma0 = %.4f, Kgamma = %.4f, sigma_cluster = %.4f\n' ...
' nlos(FLAG) = %d, gamma_rise = %.4f, gamma_1 = %.4f, chi = %.4f\n' ...
' m0 = %.4f, Km = %.4f, sigma_m0 = %.4f, sigma_Km = %.4f\n' ...
' sfading_mode(FLAG) = %d, m0_sp = %.4f, std_shdw = %.4f\n', ...
' kappa = %.4f, fc = %.4fGHz, fs = %.4fGHz\n'], ...
Lam,lambda,Lmean,lambda_mode,lambda_1,lambda_2,beta,Gam,gamma_0,Kgamma, ...
sigma_cluster,nlos,gamma_rise,gamma_1,chi,m0,Km,sigma_m0,sigma_Km,...
sfading_mode,m0_sp,std_shdw,kappa,fc,fs);
ts = 1/fs; % sampling frequency
% get a bunch of realizations (impulse responses)
[h_ct,t_ct,t0,np] = uwb_sv_model_ct_15_4a(Lam,lambda,Lmean,lambda_mode,lambda_1, ...
lambda_2,beta,Gam,gamma_0,Kgamma,sigma_cluster,nlos,gamma_rise,gamma_1, ...
chi,m0,Km,sigma_m0,sigma_Km,sfading_mode,m0_sp,std_shdw,num_channels,ts);
% now reduce continuous-time result to a discrete-time result
[hN,N] = uwb_sv_cnvrt_ct_15_4a( h_ct, t_ct, np, num_channels, ts );
if N > 1,
h = resample(hN, 1, N); % decimate the columns of hN by factor N
else
h = hN;
end
% correct for 1/N scaling imposed by decimation
% h = h * N; % normalized below..
% prepare to add the frequency dependency
K = 1; % K = Ko*Co^2/(4*pi)^2/d^n
% since the K is a constant, and the effect will be removed after
% normalization, so the K is set to be 1
h_len = length(h(:,1));
if (cm_num == 1|cm_num == 2| cm_num == 7|cm_num == 8|cm_num ==9)
[h]= uwb_sv_freq_depend_ct_15_4a(h,fc,fs,num_channels,kappa);
else
[h]= uwb_sv_freq_depend_ct_15_4a(h,fc,fs,num_channels,0);
end
%********************************************************************
% Testing and ploting
%********************************************************************
% channel energy
channel_energy = sum(abs(h).^2);
t = [0:(h_len-1)] * ts; % for use in computing excess & RMS delays
excess_delay = zeros(1,num_channels);
RMS_delay = zeros(1,num_channels);
num_sig_paths = zeros(1,num_channels);
num_sig_e_paths = zeros(1,num_channels);
for k=1:num_channels
% determine excess delay and RMS delay
sq_h = abs(h(:,k)).^2 / channel_energy(k);
t_norm = t - t0(k); % remove the randomized arrival time of first cluster
excess_delay(k) = t_norm * sq_h;
RMS_delay(k) = sqrt( ((t_norm-excess_delay(k)).^2) * sq_h );
% determine number of significant paths (paths within 10 dB from peak)
threshold_dB = -10; % dB
temp_h = abs(h(:,k));
temp_thresh = 10^(threshold_dB/20) * max(temp_h);
num_sig_paths(k) = sum(temp_h > temp_thresh);
% determine number of sig. paths (captures x % of energy in channel)
x = 0.85;
temp_sort = sort(temp_h.^2); % sorted in ascending order of energy
cum_energy = cumsum(temp_sort(end:-1:1)); % cumulative energy
index_e = min(find(cum_energy >= x * cum_energy(end)));
num_sig_e_paths(k) = index_e;
end
energy_mean = mean(10*log10(channel_energy));
energy_stddev = std(10*log10(channel_energy));
mean_excess_delay = mean(excess_delay);
mean_RMS_delay = mean(RMS_delay);
mean_sig_paths = mean(num_sig_paths);
mean_sig_e_paths = mean(num_sig_e_paths);
fprintf(1,'Model Characteristics\n');
fprintf(1,' Mean delays: excess (tau_m) = %.1f ns, RMS (tau_rms) = %1.f\n', ...
mean_excess_delay, mean_RMS_delay);
fprintf(1,' # paths: NP_10dB = %.1f, NP_85%% = %.1f\n', ...
mean_sig_paths, mean_sig_e_paths);
fprintf(1,' Channel energy: mean = %.1f dB, std deviation = %.1f dB\n', ...
energy_mean, energy_stddev);
figure(1); clf; plot(t, abs(h)); grid on
title('Impulse response realizations')
xlabel('Time (nS)')
figure(2); clf; plot([1:num_channels], excess_delay, 'b-', ...
[1 num_channels], mean_excess_delay*[1 1], 'r–' );
grid on
title('Excess delay (nS)')
xlabel('Channel number')
figure(3); clf; plot([1:num_channels], RMS_delay, 'b-', ...
[1 num_channels], mean_RMS_delay*[1 1], 'r–' );
grid on
title('RMS delay (nS)')
xlabel('Channel number')
figure(4); clf; plot([1:num_channels], num_sig_paths, 'b-', ...
[1 num_channels], mean_sig_paths*[1 1], 'r–');
grid on
title('Number of significant paths within 10 dB of peak')
xlabel('Channel number')
figure(5); clf; plot([1:num_channels], num_sig_e_paths, 'b-', ...
[1 num_channels], mean_sig_e_paths*[1 1], 'r–');
grid on
title('Number of significant paths capturing > 85% energy')
xlabel('Channel number')
temp_average_power = sum((abs(h))'.*(abs(h))')/num_channels;
temp_average_power = temp_average_power/max(temp_average_power);
average_decay_profile_dB = 10*log10(temp_average_power);
figure(6); clf; plot(t,average_decay_profile_dB); grid on
axis([0 t(end) -60 0])
title('Average Power Decay Profile')
xlabel('Delay (nsec)')
ylabel('Average power (dB)')
if 0
figure(7); clf
figh = plot([1:num_channels],10*log10(channel_energy),'b-', ...
[1 num_channels], energy_mean*[1 1], 'g–', ...
[1 num_channels], energy_mean+energy_stddev*[1 1], 'r:', ...
[1 num_channels], energy_mean-energy_stddev*[1 1], 'r:');
xlabel('Channel number')
ylabel('dB')
title('Channel Energy');
legend(figh, 'Per-channel energy', 'Mean', '\pm Std. deviation', 0)
end
if no_output_files,
return
end
%*********************************************************************
%removing the freq dependency of the antenna for cm_num=3,4,5&6
%*********************************************************************
if (cm_num == 3|cm_num == 4| cm_num == 5|cm_num == 6)
[h]= uwb_sv_freq_depend_ct_15_4a(h,fc,fs,num_channels,-0.82);
end
%**************************************************************************
%Savinge the data
%**************************************************************************
%%% save continuous-time (time,value) pairs to files
save_fn = sprintf('cm%d_imr', cm_num);
% A complete self-contained file for Matlab users
save([save_fn '.mat'], 't_ct', 'h_ct', 't0', 'np', 'num_channels', 'cm_num');
% Two comma-delimited text files for non-Matlab users:
% File #1: cmX_imr_np.csv lists the number of paths in each realization
dlmwrite([save_fn '_np.csv'], np, ','); % number of paths
% File #2: cmX_imr.csv can open with Excel
% n'th pair of columns contains the (time,value) pairs for the n'th realization
th_ct = zeros(size(t_ct,1),2*size(t_ct,2));
th_ct(:,1:2:end) = t_ct; % odd columns are time
th_ct(:,2:2:end) = h_ct; % even columns are values
fid = fopen([save_fn '.csv'], 'w');
if fid < 0,
error('unable to write .csv file for impulse response, file may be open in another application');
end
for k = 1:size(th_ct,1)
fprintf(fid,'%.4f,%.6f,', th_ct(k,1:end-2));
fprintf(fid,'%.4f,%.6f\r\n', th_ct(k,end-1:end)); % \r\n for Windoze end-of-line
end
fclose(fid);
return; % end of program
