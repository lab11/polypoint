function [h]= uwb_sv_freq_depend_ct_15_4a(h,fc,fs,num_channels,kappa)
% This function is used to remove the frequency dependency of the
% antenna(cm_num= 3,4 ,5,6) or to include the channel frequency dependency.
h_len = length(h(:,1));
f = [1-fs/fc/2 : fs/fc/h_len/2 : 1+fs/fc/2].^(-2*(kappa));
f = [f(h_len : 2*h_len), f(1 : h_len-1)]';
i = (-1)^(1/2); % complex i
for c = 1:num_channels
% add the frequency dependency
h2 = zeros(2*h_len, 1);
h2(1 : h_len) = h(:,c); % zero padding
fh2 = fft(h2);
fh2 = fh2 .* f;
h2 = ifft(fh2);
h(:,c) = h2(1:h_len);
% change to complex baseband channel
phi = rand(h_len, 1).*(2*pi);
h(:,c) = h(:,c) .* exp(phi .* i);
% Normalize the channel energy to 1
h(:,c) = h(:,c)/sqrt(h(:,c)' * h(:,c) );
end
return
