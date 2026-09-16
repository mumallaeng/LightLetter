import unittest
import numpy as np
import torch
from cnn_golden.model import Net, Quant16
from cnn_golden.train import scores


class ModelTests(unittest.TestCase):
    def test_center_identity_keeps_conv_signal(self):
        net = Net()
        for conv in net.convs:
            expected = torch.zeros_like(conv.weight)
            expected[0, 0, 1, 1] = 1.
            torch.testing.assert_close(conv.weight, expected)
            torch.testing.assert_close(conv.bias, torch.zeros_like(conv.bias))
        net.train()
        loss = net(torch.rand(4, 1, 28, 28)).square().mean()
        loss.backward()
        self.assertTrue(all(conv.weight.grad is not None and
                            torch.any(conv.weight.grad != 0).item() for conv in net.convs))

    def test_shape_and_backward(self):
        net = Net()
        output = net(torch.rand(2, 1, 28, 28))
        self.assertEqual(tuple(output.shape), (2, 36))
        output.sum().backward()
        self.assertTrue(all(p.grad is not None for p in net.parameters()))
        self.assertTrue(all(c.out_channels == 1 for c in net.convs))

    def test_inventory_matches_confirmed_spec(self):
        # C2-P1-S1-F3, FC 676->256->64->36. 실측 학습 로그(inventory)와 정확히 일치해야 한다.
        net = Net()
        inv = net.inventory()
        self.assertEqual(inv['spatial_outputs'], [27, 26])
        self.assertEqual(inv['weights'], 191762)
        self.assertEqual(inv['weight_int16_bytes'], 383524)

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
