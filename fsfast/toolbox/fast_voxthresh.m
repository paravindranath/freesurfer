function binvol = fast_voxthresh(vol,thresh,tail,invert)
% binvol = fast_voxthresh(vol,thresh,<tail>,<invert>)
%
% voxel-by-voxel binary thresholding
%
% tail can be abs, pos, neg. Default is abs.
% set invert=1 to invert the binary volume
%
% vol is input data

binvol = [];

if(nargin < 2 | nargin > 4)
  fprintf('volthresh = fast_voxthresh(vol,thresh,<tail>,<invert>)\n');
  return;
end

if(exist('tail') ~= 1) tail = ''; end
if(isempty(tail))      tail = 'abs'; end

tail = lower(tail);

if(~strcmp(tail,'abs') & ~strcmp(tail,'pos') & ~strcmp(tail,'neg'))
  fprintf('ERROR: tail = %s, must be abs, pos, or neg\n');
  return;
end

if(exist('invert') ~= 1) invert = []; end
if(isempty(invert))      invert = 0; end

if(strcmp(tail,'abs'))
  vol = abs(vol);
  tail = 'pos';
end
  
if(strcmp(tail,'pos'))
  binvol = vol > thresh;
else
  binvol = vol < -thresh;
end

if(invert == 1) binvol = ~binvol; end

return