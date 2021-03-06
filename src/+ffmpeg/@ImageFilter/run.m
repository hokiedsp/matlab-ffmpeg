function varargout = run(obj,varargin)
%FFMPEG.IMAGEFILTER.RUN   Run the filter
%   B = RUN(OBJ,A) runs the filter graph defined in OBJ given the input
%   images in A and returns the ouptut images in B. Omit A if the filter
%   graph does not require any input.
%
%   For a simple filter graph, A and B are image data arrays. A should be
%   an M-by-N-by-K array, where the depth K matching the number of
%   components of the specified pixel format in the InputFormat property.
%   The input array A can be of class uint8, single, or double. If floating
%   point, values are converted to uint8 by scaling by 255 with saturation.
%   The format of the output array B will match the input's.
%
%   If the filter graph is complex, A must be a scalar struct with its
%   fields named according to the input names of the filter graphs (given
%   in InputNames property). Each struct field should contain the image
%   data. If pixel formats are different among inputs, use similar struct
%   syntax for InputFormat property. Likewise, the output B would also be a
%   struct. To the same image is being used for some inputs, leave its
%   field value in A empty. At least one field must be non-empty.
%
%   B = RUN(OBJ, Input1Name,Input1Value,Input2Name,Input2Value,...) may be
%   used for a complex filter graph as an alternative to the struct syntax.
%   If this format is given and the filter graph only produces 1 output
%   image, the data array is given in B.

nargoutchk(0,2);

if nargin>2 % input name-data pairs
   try
      A = struct(varargin{:});
   catch
      error('Invalid input image name/value pairs given.');
   end
else
   A = varargin{1};
end

% validate input arrays: Depth will be checked in the mex function
inputs = obj.InputNames;
if isempty(inputs)
   narginchk(1,1); % does not take any input argument
   if ~isempty(obj.InputNames)
      error('The filter graph requires %d input images.',numel(obj.InputNames));
   end
elseif isstruct(A)
   if ~(isscalar(A) && isempty(setxor(inputs,fieldnames(A))))
      error('A must be a scalar struct and defines all the filter ipnut names as its fields (case sensitive)');
   end
   structfun(@(f)validateattributes(f,{'logical','uint8','single','double'},{'3d','nonsparse'}),A);
   if any(structfun(@isempty,A))
      error('At least one new image data must be given.');
   end
   type = unique(struct2cell(structfun(@(f)class(f),A,'UniformOutput',false)));
   if numel(type)>1 % if inputs are mixed type, return uint8
      type = 'uint8';
   else
      type = char(type);
   end
else
   validateattributes(A,{'logical','uint8','single','double'},{'3d','nonempty','nonsparse'});
   type = class(A);
end

% run the filter
if obj.isSimple()
   inputs = char(inputs);
   if isstruct(A), A = A.(inputs); end
   if isfloat(A), A = uint8(A*255); 
   elseif islogical(A), A = logical2uint8(A); end
   [varargout{1:nargout}] = ffmpeg.ImageFilter.mexfcn(obj,'runSimple',A);
   if isfloat(A)||islogical(A), varargout{1} = cast(varargout{1},type)/255; end
   if isstruct(A), varargout{1}.(inputs) = varargout{1}; end
else
   Nin = numel(obj.InputNames);
   for n = 1:Nin
      if isfloat(A.(inputs{n})), A.(inputs{n}) = uint8(A.(inputs{n})*255); 
      elseif islogical(A.(inputs{n})), A.(inputs{n}) = logical2uint8(A.(inputs{n})); end
   end
   [varargout{1:nargout}] = ffmpeg.ImageFilter.mexfcn(obj,'runComplex',A);
   outputs = obj.OutputNames;
   if ~strcmp(type,'uint8')
      for n = 1:numel(outputs), varargout{1}.(outputs{n}) = cast(varargout{1}.(outputs{n}),type)/255; end
   end
end
end

function B = logical2uint8(A)
B = zeros(size(A),'uint8');
B(A) = 255;
end
