import unittest
import numpy as np
import torch
from cnn_golden.model import Net, Quant16
from cnn_golden.train import scores


class ModelTests(unittest.TestCase):
    def test_shape_and_backward(self):
        net = Net()
        output = net(torch.rand(2, 1, 28, 28))
        self.assertEqual(tuple(output.shape), (2, 36))
        output.sum().backward()
        self.assertTrue(all(p.grad is not None for p in net.parameters()))
        self.assertEqual([c.in_channels for c in net.convs], [1, 6])
        self.assertEqual([c.out_channels for c in net.convs], [6, 16])

    def test_inventory_matches_confirmed_spec(self):
        # LeNet-5 3x3_schedule, FC 400->120->84->36. 실측 학습 로그(inventory)와 정확히 일치해야 한다.
        net = Net()
        inv = net.inventory()
        self.assertEqual(inv['spatial_outputs'], [13, 5])
        self.assertEqual(inv['weights'], 62022)
        self.assertEqual(inv['weight_int16_bytes'], 124044)
        self.assertEqual(inv['biases'], 262)
        self.assertEqual(inv['bias_int32_bytes'], 1048)

    def test_grid_rounding_clipping_and_gradient(self):
        quant = Quant16().eval()
        quant.maximum.fill_(32767.)
        values = torch.tensor([-40000., -2.5, -1.5, .5, 1.5, 2.5, 40000.], requires_grad=True)
        output = quant(values)
        torch.testing.assert_close(output, torch.tensor([-32768., -2., -2., 0., 2., 2., 32767.]))
        output.sum().backward()
        torch.testing.assert_close(values.grad, torch.tensor([0., 1., 1., 1., 1., 1., 0.]))

    def test_eval_does_not_observe(self):
        quant = Quant16()
        quant(torch.tensor([1.]))
        before = quant.maximum.clone()
        quant.eval()(torch.tensor([999.]))
        torch.testing.assert_close(before, quant.maximum)

    def test_bias_quantized_with_derived_scale(self):
        # Standard integer-inference scheme (Jacob et al. 2017): bias_scale is derived as
        # input_scale * weight_scale, not independently observed like weight/activation.
        net = Net()
        net.input_quant.maximum.fill_(32767.)      # input_scale == 1.0
        net.weight_quant[0].maximum.fill_(32767.)  # weight_scale == 1.0
        bias = torch.tensor([0.3, -0.3, 100.7, 5.5, -5.5, 0.1])
        quantized = net._quantized_bias(0, bias)
        # bias_scale == 1.0 * 1.0 == 1.0 here, so quantizing rounds to the nearest integer.
        torch.testing.assert_close(quantized, bias.round())

    def test_bias_quantization_disabled_with_quantization_toggle(self):
        net = Net()
        net.quantization(False)
        bias = torch.tensor([0.123456])
        torch.testing.assert_close(net._quantized_bias(0, bias), bias)

    def test_metric_counts(self):
        y = np.arange(36)
        p = y.copy()
        p[0] = 1
        result = scores(y, p)
        self.assertEqual(result['correct'], 35)
        self.assertEqual(result['letter_accuracy'], 1.)

    @unittest.skipUnless(torch.backends.mps.is_available(), 'MPS GPU unavailable')
    def test_mps_training(self):
        net = Net().to('mps')
        optimizer = torch.optim.Adam(net.parameters(), lr=.001)
        loss = torch.nn.functional.cross_entropy(net(torch.rand(128, 1, 28, 28, device='mps')),
                                                 torch.arange(128, device='mps') % 36)
        loss.backward()
        optimizer.step()
        self.assertTrue(torch.isfinite(loss).item())


if __name__ == '__main__':
    unittest.main()
