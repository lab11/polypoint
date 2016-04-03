function ret = srl_to_double(in_array)

ret = 0;
for ii=length(in_array):-1:1
	ret = ret*256;
	ret = ret+double(in_array(ii));
end
