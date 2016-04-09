function [h,t,t0,np] = uwb_sv_model_ct_15_4a(Lam,lambda,Lmean,lambda_mode,lambda_1, ...
lambda_2,beta,Gam,gamma_0,Kgamma,sigma_cluster,nlos,gamma_rise,gamma_1, ...
chi,m0,Km,sigma_m0,sigma_Km,sfading_mode,m0_sp,std_shdw,num_channels,ts)
% Written by Sun Xu, Kim Chee Wee, B. Kannan & Francois Chin on 14/09/2004
% IEEE 802.15.4a UWB channel model for PHY proposal evaluation
% continuous-time realization of modified S-V channel model
% Input parameters:
% detailed introduction of input parameters is at uwb_sv_params.m
% num_channels number of random realizations to generate
% Outputs
% h is returned as a matrix with num_channels columns, each column
% holding a random realization of the channel model (an impulse response)
% t is organized as h, but holds the time instances (in nsec) of the paths whose
% signed amplitudes are stored in h
% t0 is the arrival time of the first cluster for each realization
% np is the number of paths for each realization.
% Thus, the k'th realization of the channel impulse response is the sequence
% of (time,value) pairs given by (t(1:np(k),k), h(1:np(k),k))
%
% modified by I2R
% initialize and precompute some things
std_L = 1/sqrt(2*Lam); % std dev (nsec) of cluster arrival spacing
std_lam = 1/sqrt(2*lambda); % std dev (nsec) of ray arrival spacing
h_len = 1000; % there must be a better estimate of # of paths than this
ngrow = 1000; % amount to grow data structure if more paths are needed
h = zeros(h_len,num_channels);
t = zeros(h_len,num_channels);
t0 = zeros(1,num_channels);
np = zeros(1,num_channels);
for k = 1:num_channels % loop over number of channels
tmp_h = zeros(size(h,1),1);
tmp_t = zeros(size(h,1),1);
if nlos == 1,
Tc = (std_L*randn)^2 + (std_L*randn)^2; % First cluster random arrival
else
Tc = 0; % First cluster arrival occurs at time 0
end
t0(k) = Tc;
if nlos == 2 & lambda_mode == 2
L = 1; % for industrial NLOS environment
else
L = max(1, poissrnd(Lmean)); % number of clusters
end
cluster_index = zeros(1,L);
path_ix = 0;
nak_m = [];
for ncluster = 1:L
% Determine Ray arrivals for each cluster
Tr = 0; % first ray arrival defined to be time 0 relative to cluster
cluster_index(ncluster) = path_ix+1; % remember the cluster location
gamma = Kgamma*Tc + gamma_0; % delay dependent cluster decay time
if nlos == 2 & ncluster == 1
gamma = gamma_1;
end
Mcluster = sigma_cluster*randn;
Pcluster = 10*log10(exp(-1*Tc/Gam))+Mcluster; % total cluster power
Pcluster = 10^(Pcluster*0.1);
while (Tr < 10*gamma),
t_val = (Tc+Tr); % time of arrival of this ray
if nlos == 2 & ncluster == 1
h_val = Pcluster*(1-chi*exp(-Tr/gamma_rise))*exp(-Tr/gamma_1) ...
*(gamma+gamma_rise)/gamma/(gamma+gamma_rise*(1-chi));
else
h_val = Pcluster/gamma*exp(-Tr/gamma);
end
path_ix = path_ix + 1; % row index of this ray
if path_ix > h_len,
% grow the output structures to handle more paths as needed
tmp_h = [tmp_h; zeros(ngrow,1)];
tmp_t = [tmp_t; zeros(ngrow,1)];
h = [h; zeros(ngrow,num_channels)];
t = [t; zeros(ngrow,num_channels)];
h_len = h_len + ngrow;
end
tmp_h(path_ix) = h_val;
tmp_t(path_ix) = t_val;
if lambda_mode == 0
Tr = Tr + (std_lam*randn)^2 + (std_lam*randn)^2;
elseif lambda_mode == 1
if rand < beta
std_lam = 1/sqrt(2*lambda_1);
Tr = Tr + (std_lam*randn)^2 + (std_lam*randn)^2;
else
std_lam = 1/sqrt(2*lambda_2);
Tr = Tr + (std_lam*randn)^2 + (std_lam*randn)^2;
end
elseif lambda_mode == 2
Tr = Tr + ts;
else
error('lambda mode is wrong!')
end
% generate log-normal distributed nakagami m-factor
m_mu = m0 - Km*t_val;
m_std = sigma_m0 - sigma_Km*t_val;
nak_m = [nak_m, lognrnd(m_mu, m_std)];
end
Tc = Tc + (std_L*randn)^2 + (std_L*randn)^2;
end
% change m value of the first multipath to be the deterministic value
if sfading_mode == 1
nak_ms(cluster_index(1)) = m0_sp;
elseif sfading_mode == 2
nak_ms(cluster_index) = m0_sp;
end
% apply nakagami
for path = 1:path_ix
h_val = (gamrnd(nak_m(path), tmp_h(path)/nak_m(path))).^(1/2);
tmp_h(path) = h_val;
end
np(k) = path_ix; % number of rays (or paths) for this realization
[sort_tmp_t,sort_ix] = sort(tmp_t(1:np(k))); % sort in ascending time order
t(1:np(k),k) = sort_tmp_t;
h(1:np(k),k) = tmp_h(sort_ix(1:np(k)));
% now impose a log-normal shadowing on this realization
% fac = 10^(std_shdw*randn/20) / sqrt( h(1:np(k),k)' * h(1:np(k),k) );
% h(1:np(k),k) = h(1:np(k),k) * fac;
end
return
