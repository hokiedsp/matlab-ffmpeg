classdef base < handle & matlab.mixin.Heterogeneous
%FFMPEGFILTER.BASE   Abstract heterogeneous base class for FFMPEGFILTER classes

   properties (Constant, Abstract)
      nin  % range of number of input ports
      nout % range of number of output ports
   end
   methods (Abstract=true, Access=protected)
      str = print_filter(obj) % guaranteed scalar
   end
   properties (SetAccess=protected)
      inports  = ffmpegfilter.null.empty() % filters connected to this input
      outports = ffmpegfilter.null.empty() % filters connected to this output
      
      inlabels = {}    % (unique) names of the links connecting input ports to the source filters
      outlabels = {}  % (unique) names of the links connecting output ports to the destination filters
   end
   properties
      out_cnt   % counters used by ffmpegfiltergraph
   end
   methods (Sealed, Static, Access = protected)
      function default_object = getDefaultScalarElement
         default_object = ffmpegfilter.null;
      end
   end
   methods
      function delete(obj)
         try
            removelinks(obj);
         catch
         end
      end
   end
   methods (Sealed)
      function tf = issimplechain(filters)
         %FFMPEGFILTER.BASE.ISSIMPLECHAIN   True if simple chain of filters
         %   ISSIMPLECHAIN(FILTERS) returns true if none of FFMPEGFILTER objects in
         %   FILTERS array contains a link and contains neither FFMPEGFILTER.HEAD
         %   nor FFMPEGFILTER.TAIL object. Such condition indicates that the filter
         %   order has not been defined among objects in FILTERS, and it could be
         %   perceived as FILTERS array represents a single chain of FFmpeg
         %   filtering operation in the column-first order.

         tf =  all(arrayfun(@(f)isempty(f.inports)&&isempty(f.outports),filters)) ...
            && ~any(ismember({'ffmpegfilter.head','ffmpegfilter.tail'},arrayfun(@class,filters,'UniformOutput',false)));
      end
      function removelinks(obj)
         for n = 1:numel(obj)
            % delete the output
            linkedobjs = obj(n).outports;
            obj(n).outports(:) = [];
            for m = 1:numel(linkedobjs)
               linkedobjs(m).inports(:) = [];
            end
            
            linkedobjs = obj(n).inports;
            obj(n).inports(:) = [];
            for m = 1:numel(linkedobjs)
               linkedobjs(m).outports(:) = [];
            end
         end
      end
      function link(src,dst,labels,append)
         
         narginchk(2,4);
         
         validateattributes(src,{'ffmpegfilter.base'},{});
         validateattributes(dst,{'ffmpegfilter.base'},{});

         if nargin<4
            append = false; % overwrite current link by default
         else
            validateattributes(append,{'logical'},{'scalar'});
         end
         
         nsrc = numel(src);
         ndst = numel(dst);
         
         if nsrc~=1 && ndst~=1
            error('Either SRC or DST must be a scalar object.');
         end
         if nargin>2
            if ischar(labels)
               labels = {labels};
            end
            if max(nsrc,ndst)~=numel(labels)
               error('Number of labels does not match the number of links.');
            end
            if ~iscellstr(labels) && cellfun(@(s)isrow(s)&&~any(isspace(s)))
               error('Link labels must be given as a cell array of strings, each of which does not contain any white space.');
            end
         elseif max(nsrc,ndst)>1
            error('Links with multiple source/destination filters require labels.');
         end
         
         % inspect the label's uniqueness
         if nargin>2 
            base_labels = labels;
            k = 1;
            while ~inspectnewlabels([src(:);dst(:)],ffmpegfilter.null.empty(),labels)
               labels = strcat(base_labels,num2str(k));
               k = k + 1;
            end
         end
         
         % create the links
         if append
            for n = 1:ndst
               dst(n).inports(end+1:end+nsrc) = src;
            end
            for n = 1:nsrc
               src(n).outports(end+1:end+ndst) = dst;
            end
            
            if nargin>2
               
               nlabels = max(nsrc,ndst);
               
               % insert the labels

               if isscalar(src) % SIMO/SISO
                  src.outlabels(end+1:end+nlabels) = labels;
               else % MISO
                  for n = 1:nsrc
                     src(n).outlabels(end+1) = labels(n);
                  end
               end
               if isscalar(dst) % SISO/MISO
                  dst.inlabels (end+1:end+nlabels)= labels;
               else % SI/MO
                  for n = 1:ndst
                     dst(n).inlabels(end+1) = labels(n);
                  end
               end
            end
         else
            [dst.inports] = deal(src);
            [src.outports] = deal(dst);
            
            if nargin>2
               if isscalar(src)
                  src.outlabels = labels;
               else
                  for n = 1:nsrc
                     src(n).outlabels = labels(n);
                  end
               end
               if isscalar(dst)
                  dst.inlabels = labels;
               else
                  for n = 1:ndst
                     dst(n).inlabels = labels(n);
                  end
               end
            end
         end
         
      end
      
      function str = print(obj)
         
         % FILTER           ::= [LINKLABELS] NAME ["=" FILTER_ARGUMENTS] [LINKLABELS]
         
         str = '';
         for n = 1:numel(obj.inlabels)
            str = sprintf('%s[%s]',str,obj.inlabels{n});
         end
         str = sprintf('%s%s',str,obj.print_filter());
         for n = 1:numel(obj.outlabels)
            str = sprintf('%s[%s]',str,obj.outlabels{n});
         end
      end
      
      function TF = eq(A,B)
         TF = eq@handle(A,B);
      end
      
      function TF = ne(A,B)
         TF = ne@handle(A,B);
      end
   end
   
   methods (Sealed, Access=private)
      function [tf,objs] = inspectnewlabels(obj,objs,newlabels)

         % check the input port labels
         tf = ~any(arrayfun(@(l)any(strcmp(l,[obj.inlabels])),newlabels));
         
         % keep all previously visited objects to prevent infinite looping
         if isempty(objs)
            objs = obj;
         else
            objs = union(objs,obj);
         end
         
         h = setdiff([obj.inports],objs);
         if tf && ~isempty(h)
            [tf,objs] = inspectnewlabels(h,objs,newlabels);
         end
         
         h = setdiff([obj.outports],objs);
         if tf && ~isempty(h)
            [tf,objs] = inspectnewlabels(h,objs,newlabels);
         end
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
