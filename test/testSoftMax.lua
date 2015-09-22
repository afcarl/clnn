local _test = clnn._test
local times = _test.times
local clnntest = _test.clnntest
local x_clnntest = _test.x_clnntest
local nloop = _test.nloop
local precision_forward = 1e-4
local precision_backward = 1e-2

function x_clnntest.SoftMax_forward()
   local size = math.random(1,100)
   
   local tm = {}
   local title = string.format('SoftMax forward %d -> %d', size, size)
   times[title] = tm
   
   local input = torch.randn(size)
   local sconv = nn.SoftMax()
   local groundtruth = sconv:forward(input)
   local a = torch.Timer()
   for i = 1,nloop do
      groundtruth = sconv:forward(input)
   end
   tm.cpu = a:time().real
   
   input = input:cl()
   local gconv = nn.SoftMax():cl()
   local rescl = gconv:forward(input)
   a:reset()
   for i = 1,nloop do
      rescl = gconv:forward(input)
   end
   cltorch.synchronize()
   tm.gpu = a:time().real
   
   local error = rescl:float() - groundtruth
   mytester:assertlt(error:abs():max(), precision_forward, 'error on state (forward) ')
end

function x_clnntest.SoftMax_backward()
   local size = math.random(1,100)
   
   local tm = {}
   local title = string.format('SoftMax.backward %d -> %d', size, size)
   times[title] = tm
   
   local input = torch.randn(size)
   local gradOutput = torch.randn(size)
   local sconv = nn.SoftMax()
   sconv:forward(input)
   local groundgrad = sconv:backward(input, gradOutput)
   local a = torch.Timer()
   for i = 1,nloop do
      groundgrad = sconv:backward(input, gradOutput)
   end
   tm.cpu = a:time().real
   
   input = input:cl()
   gradOutput = gradOutput:cl()
   local gconv = sconv:clone():cl()
   gconv:forward(input)
   local rescl = gconv:backward(input, gradOutput)
   a:reset()
   for i = 1,nloop do
      rescl = gconv:backward(input, gradOutput)
   end
   cltorch.synchronize()
   tm.gpu = a:time().real
   
   local error = rescl:float() - groundgrad
   
   mytester:assertlt(error:abs():max(), precision_backward, 'error on state (backward) ')
end
