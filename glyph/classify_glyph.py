#!/usr/bin/env python3

# Classify a font glyph by predicting glyph unicode value from glyph bitmap
import sys
import argparse
import matplotlib.pyplot as plt
import numpy as np
import re
import copy
import time
import gzip
import pandas as pd
import struct # For reading packed binary data
import random

# Machine learning packages
from sklearn import datasets, metrics
from sklearn.model_selection import train_test_split

from sklearn import naive_bayes
from sklearn import svm
from sklearn import ensemble
from sklearn import neighbors
from sklearn import neural_network
from sklearn import linear_model
from sklearn.utils import class_weight

import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torchvision import datasets, transforms
from torch.utils.data import Dataset, DataLoader, Sampler
from torch.utils.data.sampler import SequentialSampler
# from torcheval.metrics import MulticlassAccuracy <-- Could use for macro average but wants number of classes as argument

from typing import List, Dict, Optional

CLASSIFY_GLYPH_VERSION = '0.2 October 7, 2025'
#CLASSIFY_GLYPH_VERSION = '0.1 May 21, 2025'
# Initial version

class Glyph:
    def __init__(self, m_unicode = 0, m_width = 0, m_height = 0, m_count = 0):
        self.unicode = m_unicode
        self.width = m_width
        self.height = m_height
        self.count = m_count
        self.bitmap = np.array([])

########################################################
# Define a custom dataset
class DeepDataset(Dataset):
    def __init__(self, data, labels):
        self.data = data
        self.labels = labels
    
    def __len__(self):
        return len(self.data)
    
    def __getitem__(self, idx):
        return self.data[idx], self.labels[idx]

class SimpleMLP(nn.Module):
    def __init__(self, input_size, hidden_sizes, output_size,dropout=0.3):
        super(SimpleMLP, self).__init__()

        layers = []

        if len(hidden_sizes) == 0:
            layers.append( nn.Linear(input_size, output_size, bias=True) )
        else:

            # Input layer
            layers.append( nn.Linear(input_size, hidden_sizes[0], bias=True) )
            layers.append( nn.ReLU() )
            layers.append(nn.Dropout(dropout)) 

            for i in range( len(hidden_sizes) - 1):
                layers.append( nn.Linear(hidden_sizes[i], hidden_sizes[i+1], bias=True) )
                layers.append( nn.ReLU() )
                layers.append(nn.Dropout(dropout)) 

            # Ouput layer
            layers.append( nn.Linear(hidden_sizes[-1], output_size, bias=True) )

        self.layers = nn.Sequential(*layers)

    def forward(self, x):
        return self.layers( torch.flatten(x, 1) )

# Provided by ChatGPT 5.1
class ClassBalancedBatchSampler(Sampler[List[int]]):
    """
    A batch sampler that yields batches with roughly
    `classes_per_batch` distinct classes and
    `samples_per_class` samples *per* selected class.

    Batch size = classes_per_batch * samples_per_class

    Parameters
    ----------
    class_to_indices : dict[int, list[int]]
        Mapping from class label to list of dataset indices.
    classes_per_batch : int
        How many distinct classes to sample in each batch.
    samples_per_class : int
        How many samples to draw per selected class in each batch.
    num_batches : int, optional
        How many batches to generate per epoch. If None, it will be
        approximated using the total number of samples.
    with_replacement : bool
        If True, sample examples from each class with replacement.
        Useful for very rare classes.
    class_sampling_weights : Optional[torch.Tensor]
        A 1D tensor of shape [num_classes] giving a sampling weight
        per class (e.g. inverse frequency). If None, classes are
        sampled uniformly.
    """

    def __init__(
        self,
        class_to_indices: Dict[int, List[int]],
        classes_per_batch: int,
        samples_per_class: int,
        num_batches: Optional[int] = None,
        with_replacement: bool = True,
        class_sampling_weights: Optional[torch.Tensor] = None,
    ):
        super().__init__(None)

        self.class_to_indices = {k: v for k, v in class_to_indices.items() if len(v) > 0}
        self.classes = sorted(self.class_to_indices.keys())
        self.num_classes = len(self.classes)
        self.classes_per_batch = min(classes_per_batch, self.num_classes)
        self.samples_per_class = samples_per_class
        self.with_replacement = with_replacement

        # Default: uniform over classes
        if class_sampling_weights is None:
            self.class_sampling_weights = torch.ones(self.num_classes, dtype=torch.float)
        else:
            assert class_sampling_weights.numel() == self.num_classes, \
                "class_sampling_weights must have shape [num_classes]"
            self.class_sampling_weights = class_sampling_weights.float()

        # Normalize weights -- update: the torch.multinomial function does not require normalized weights
        #self.class_sampling_weights = self.class_sampling_weights /self.class_sampling_weights.sum()

        # Estimate num_batches if not provided
        if num_batches is None:
            total_samples = sum(len(v) for v in self.class_to_indices.values())
            approx_batch_size = self.classes_per_batch * self.samples_per_class
            self.num_batches = max(1, total_samples // approx_batch_size)
        else:
            self.num_batches = num_batches

    def __len__(self) -> int:
        return self.num_batches

    def __iter__(self):

        for _ in range(self.num_batches):
            # 1) Sample classes_per_batch distinct classes (or with replacement if needed)
            if self.classes_per_batch == self.num_classes:
                selected_classes = self.classes
            else:
                # sample class indices according to weights
                # we sample with replacement in class space first, then deduplicate if desired
                sampled_class_idxs = torch.multinomial(
                    self.class_sampling_weights,
                    num_samples=self.classes_per_batch,
                    replacement=True,
                ).tolist()
                # map to actual class labels
                selected_classes = [self.classes[i] for i in sampled_class_idxs]

                # If you'd like strictly distinct classes, you can deduplicate,
                # but this might reduce the effective classes_per_batch.
                # selected_classes = list(dict.fromkeys(selected_classes))

            batch_indices = []

            # 2) For each class, sample samples_per_class indices
            for c in selected_classes:
                idxs = self.class_to_indices[c]
                if self.with_replacement or len(idxs) < self.samples_per_class:
                    chosen = random.choices(idxs, k=self.samples_per_class)
                else:
                    chosen = random.sample(idxs, k=self.samples_per_class)
                batch_indices.extend(chosen)

            yield batch_indices

# Perturb the training data with random shifts in X and Y
def jitter_batch_int(x, max_shift):
    """
    x: [B, C, H, W]
    integer shifts with zero padding, no interpolation
    """
    B, C, H, W = x.shape
    device = x.device

    dx = torch.randint(-max_shift, max_shift+1, (B,), device=device)
    dy = torch.randint(-max_shift, max_shift+1, (B,), device=device)

    #out = torch.zeros_like(x) #<-- 0 fill (since the background is white == 1.0 this provides a visual indication of jitter offsets)
    out = torch.full_like(x, fill_value=1.0) # Fill with 1.0 (which is the scaled version of 255 and equivalent to a white background)

    for i in range(B):
        sx = dx[i].item()
        sy = dy[i].item()

        x_start = max(0, sx)
        x_end   = W + min(0, sx)
        xs_src  = max(0, -sx)
        xe_src  = W - max(0, sx)

        y_start = max(0, sy)
        y_end   = H + min(0, sy)
        ys_src  = max(0, -sy)
        ye_src  = H - max(0, sy)

        out[i, :, y_start:y_end, x_start:x_end] = \
            x[i, :, ys_src:ye_src, xs_src:xe_src]

    return out

# Visualize the glyph data
def show_batch(x, nrows=2, ncols=4, cmap="gray"):
    """
    x : tensor of shape [B, 1, H, W] or [B, H, W]
    Visualizes the first nrows*ncols images in the batch.
    """
    # Ensure batch has channel dimension
    if x.dim() == 3:        # [B, H, W]
        x = x.unsqueeze(1)  # → [B, 1, H, W]

    B, C, H, W = x.shape
    num = min(B, nrows*ncols)

    fig, axes = plt.subplots(nrows, ncols, figsize=(ncols*3, nrows*3))
    axes = axes.flatten()

    for i in range(num):
        img = x[i].squeeze().cpu().numpy()   # remove batch/channel dims

        # --- horizontal flip (top↔bottom) ---
        img = np.flipud(img)

        axes[i].imshow(img, cmap=cmap)
        axes[i].set_title(f"Sample {i}")
        axes[i].axis("off")

    # Hide any unused subplots
    for j in range(num, nrows*ncols):
        axes[j].axis("off")

    plt.tight_layout()
    plt.show()

def main():

    endianness = 'little' # For reading binary data
    max_glyph_count = -1 # Max glyph count per unicode value. Set to a a value >= zero to use all
    min_glyph_count = 10 # Minimum number of glyphs to include in test and training

    parser = argparse.ArgumentParser( description='Predict font uniocode values from glyph bitmaps v.{}'. format(CLASSIFY_GLYPH_VERSION), formatter_class=argparse.ArgumentDefaultsHelpFormatter )
    
    parser.add_argument('-i', dest='font_data_filename', help='Input filename of font glyph bitmaps')
    parser.add_argument('--param', dest='param_filename', help='Output filename of model parameters')
	
    args = parser.parse_args()

    if not args.font_data_filename:
        sys.stderr.write('Please specify a file of font glyph bitmaps\n')
        sys.exit(0)

    try:
        fin = gzip.open(args.font_data_filename)
    except OSError:
        sys.stderr.write('Unable to open {} for reading\n'.format(filename))
        sys.exit(0)

    # Count the number and abundance of different unicode values
    unicode_count = dict()

    if max_glyph_count > 0:
        print('Per-unicode glyph count is capped at {}'.format(max_glyph_count), file=sys.stderr, flush=True)

    print('Loading glyph bitmaps ... ', end='', file=sys.stderr, flush=True)

    glyphs = []
    
    # Define struct format once (outside the while loop)
    # Assuming little-endian: I=unsigned int(4), Q=unsigned long long(8), i=signed int(4)
    if endianness == 'little':
        header_format = '<IQii'  # unicode, count, width, height
    else:
        header_format = '>IQii'

    header_size = struct.calcsize(header_format)

    while True:

        g = Glyph()

        # Read entire header in one operation (20 bytes total)
        header_buffer = fin.read(header_size)
        
        if len(header_buffer) == 0:
            print('Finished reading input file', flush=True)
            break
            
        if len(header_buffer) != header_size:
            sys.stderr.write('Incomplete header read\n')
            sys.exit(0)
    
        # Unpack all header fields at once
        g.unicode, g.count, g.width, g.height = struct.unpack(header_format, header_buffer)

        bmp_size = g.width * g.height

        buffer = fin.read(bmp_size)

        if len(buffer) != bmp_size:
            sys.stderr.write('Unable to read {} by {} pixel glyph bitmpa\n'.format(g.width, g.height))
            sys.exit(0)

        # Convert bytes directly to numpy array and rescale (MUCH faster!). My initial implementation
        # used a for-loop to copy individual bytes (thanks to Claude 4.5 Sonnet for the optimization!)
        g.bitmap = np.frombuffer(buffer, dtype=np.uint8).astype(np.float32) / 255.0
        # Instead of rescaling the glyph bitmaps prior to training (which would need to be done for every image when using the
        # model for for inference), we will rescale the initial model parameters by 255.0 prior to training (see below).
        # Note that if we don't scale either the data or the model parameters, then the training fails to decrease the loss.
        #g.bitmap = np.frombuffer(buffer, dtype=np.uint8).astype(np.float32)
        g.bitmap = g.bitmap.reshape(g.width, g.height) # Reshape into a 2D image

        if g.unicode not in unicode_count:
            unicode_count[g.unicode] = 0

        unicode_count[g.unicode] += 1

        if (max_glyph_count <= 0) or (unicode_count[g.unicode] <= max_glyph_count):
            glyphs.append(copy.copy(g))
        
    fin.close()

    print('done.', file=sys.stderr)

    print('Loaded {} glyphs'.format(len(glyphs)), file=sys.stderr)

    # Flipping bitmaps is no longer needed
    # Flip the bitmaps about the X axis (i.e., x' = x, y' = (height - 1) - y) to match the native bmp format used by xpdf/poppler
    #for i in range(len(glyphs)):
    #    glyphs[i].bitmap = np.flip(glyphs[i].bitmap, axis=0)
    #print('Flipped bitmaps about X to match native xpdf-poppler format'.format(file=sys.stderr))
    
    print('The valid glyphs represent {} unicode values'.format(len(unicode_count)), file=sys.stderr)

    # Label encoding is needed for PyTorch (even though unicode values are already integers) -- Thanks Claude 4.5 Sonnet!
    # SKlearn handles this automatically ...
    unique_unicodes = sorted(set(g.unicode for g in glyphs))
    unicode_to_idx = {unicode_val: idx for idx, unicode_val in enumerate(unique_unicodes)}
    idx_to_unicode = {idx: unicode_val for unicode_val, idx in unicode_to_idx.items()}

    data = []
    labels = []

    for g in glyphs:
        
        g.bitmap.resize( (1,) + g.bitmap.shape) # Reshape, since pytorch expects the first dimension to be the "channel" dimension (even through we only have a single channel)
        data.append( torch.from_numpy(g.bitmap) )
        labels.append(unicode_to_idx[g.unicode]) # Class index values (*NOT* raw unicode values!)

    
    # Extend the device selection to include 'cuda' if needed
    device = 'mps' if torch.backends.mps.is_available() else 'cpu'
    
    ###########################################################################
    # Full glyph training data: PMC_font_db_train.bin.gz
    # [3600] @ epoch 625 --> Test set: average loss 1.0312; micro accuracy: 112043/145002 (77.27%); macro accuracy = 88.60%; W/O Linear bias terms;log sample weights
    # [3600, 3600] @ epoch 350 --> Test set: average loss 0.3426; micro accuracy: 138750/145002 (95.69%); macro accuracy = 94.52%; w/ Linear bias terms;log sample weights
    # [3600, 3600] @ epoch 275 --> Test set: average loss 0.3283; micro accuracy: 139433/145002 (96.16%); macro accuracy = 94.99%; W/O Linear bias terms;log sample weights
    # [3600, 3600] @ epoch 175 --> Test set: average loss 0.2669; micro accuracy: 139862/145002 (96.46%); macro accuracy = 94.57%; W/O Linear bias terms;sqrt sample weights
    # [3600, 3600] @ epoch 975 --> Test set: average loss 0.3305; micro accuracy: 135085/145002 (93.16%); macro accuracy = 94.90%; W/O Linear bias terms; 30% dropout;log sample weights
    # [3600, 3600] @ epoch 200 --> Test set: average loss 0.5036; micro accuracy: 138849/145002 (95.76%); macro accuracy = 93.12%; W/O Linear bias terms;sqrt sample weights;unscaled data, scaled model
    # [3600, 3600] @ epoch 200 --> Test set: average loss 0.2546; micro accuracy: 140740/145002 (97.06%); macro accuracy = 94.76%; W/O Linear bias terms;sqrt sample weights;scaled data, unscaled model
    # [3600, 3600, 3600] @ epoch 975 --> Test set: average loss 0.4296; micro accuracy: 138786/142911 (97.11%); macro accuracy = 95.25%; W/O Linear bias terms;log sample weights
    # [3600, 2500, 2000, 1000] @ epoch 550 --> Test set: average loss 0.3429; micro accuracy: 140096/145002 (96.62%); macro accuracy = 94.83%; W/O Linear bias terms;log sample weights

    ###########################################################################
    # Reduced glyph training data: PMC_font_db_train_reduced.bin.gz
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.2313; micro accuracy: 128169/130553 (98.17%); macro accuracy = 90.04% W/O bias
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.2189; micro accuracy: 128324/130553 (98.29%); macro accuracy = 90.97% with bias
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.1032; micro accuracy: 127810/130553 (97.90%); macro accuracy = 91.86% with bias & 40% dropout
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.1037; micro accuracy: 127510/129900 (98.16%); macro accuracy = 93.71% with bias & 30% dropout
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.1270; micro accuracy: 128258/130553 (98.24%); macro accuracy = 92.24% with bias & 25% dropout
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.1370; micro accuracy: 128209/130553 (98.20%); macro accuracy = 92.24% with bias & 20% dropout
    # [3600, 3600] @ epoch 700 --> Test set: average loss 0.1756; micro accuracy: 128431/130553 (98.37%); macro accuracy = 91.86% with bias & 10% dropout
    #
    # [3000, 2000] @ epoch 700 --> Test set: average loss 0.0920; micro accuracy: 127611/129900 (98.24%); macro accuracy = 93.67% with bias & 30% dropout
    # [3000, 2000] @ epoch 8275 --> Test set: average loss 0.1609; micro accuracy: 124597/130553 (95.44%); macro accuracy = 81.29% with bias, 30% dropout, 5x5 jitter
    # [3000, 2000] @ epoch 1750 --> Test set: average loss 0.0892; micro accuracy: 127832/130553 (97.92%); macro accuracy = 91.19% with bias, 30% dropout, 1x1 jitter

    #layers = [3600, 3600]
    layers = [3000, 2000]

    num_epoch = 200 # <-- increase the number of epochs when using jittering
    classes_per_batch = 100
    samples_per_class = 10
    learning_rate = 0.0001      
    num_batches_per_epoch = None #1000  # or leave None to auto-estimate
    num_fold = 1

    print('Performing {}-fold cross validation with {} epochs, {} unicode classes per epoch and {} glyphs per class'.format(num_fold, num_epoch, classes_per_batch, samples_per_class))
    print('Learning rate is {}'.format(learning_rate))

    deep_data = DeepDataset(data, labels)

    deep_data_size = len(deep_data)
    indices = list(range(deep_data_size))

    np.random.shuffle(indices) # Shuffle before partioning into folds

    # For ensuring that each fold has a representative number of data points for each class
    class_to_indices = dict() # For class balanced sampling: unicode value -> list of training indicies

    for i in range(deep_data_size):
        idx = indices[i]
        value = labels[idx]

        if value not in class_to_indices:
            class_to_indices[value] = list()
        
        class_to_indices[value].append(idx)

    most_abundant_unicode = None
    least_abundant_unicode = None

    for value, idx_list in class_to_indices.items():
        if most_abundant_unicode is None:
            most_abundant_unicode = len(idx_list)
        else:
            most_abundant_unicode = max(most_abundant_unicode, len(idx_list))

        if least_abundant_unicode is None:
            least_abundant_unicode = len(idx_list)
        else:
            least_abundant_unicode = min(least_abundant_unicode, len(idx_list))

    print('Most abuntant unicode value has {} glyphs, the least abundant has {} glyphs'.format(most_abundant_unicode, least_abundant_unicode))

    all_classes_present_in_test_and_training = True # Test to make sure we don't omit a unicode value

    for fold in range(num_fold):

        if(num_fold > 1):
            print('Cross validation fold {}'.format(fold))
        else:
            print('Using all training data (no cross validation)')

        test_indices = []
        train_indices = []

        training_class_to_indices = dict() # For per-fold class balanced sampling: unicode value -> list of training indicies

        for value, idx_list in class_to_indices.items():
            
            has_test_data = False
            has_training_data = False

            for i in range(len(idx_list)):
                
                idx = idx_list[i]

                if (num_fold > 1) and (i%num_fold == fold): # Test

                    test_indices.append(idx)
                    has_test_data = True
                else: # Train

                    train_indices.append(idx)
                    has_training_data = True

                    if value not in training_class_to_indices:
                        training_class_to_indices[value] = list()
                    
                    training_class_to_indices[value].append(idx)
            
            if not (has_test_data and has_training_data):
                all_classes_present_in_test_and_training = False

        print('\t|train| = {}'.format(len(train_indices)))
        print('\t|test| = {}'.format(len(test_indices)))

        if num_fold > 1:
            if all_classes_present_in_test_and_training:
                print('\tAll unicode values present in test and training')
            else:
                print('\tNOT all unicode values are present in test and training')

        # The probability of selecting a given unicode value during batch sampling. Note that these values do not need to be normalized here
        # (they will be normalized when ClassBalancedBatchSampler is initialized)
        class_counts = torch.tensor([len(training_class_to_indices[c]) for c in sorted(training_class_to_indices.keys())], dtype=torch.float)
        ##class_sampling_weights = 1.0 / (class_counts + 1e-6) # Compute simple inverse-frequency class weights suggested by ChatGPT, doesn't make sense to me!
        
        #class_sampling_weights = torch.pow(class_counts, 0.0)
        class_sampling_weights = torch.pow(class_counts, 0.5)
        #class_sampling_weights = torch.pow(class_counts, 1.0)

        # Note that omitting the dtype=torch.float argument to torch.ones prevents the loss from decreasing during training. Why?
        #class_sampling_weights = torch.ones( len(training_class_to_indices), dtype=torch.float ) # All classes are equally likely 
        #class_sampling_weights = torch.ones( len(training_class_to_indices), dtype=torch.float )
        #class_sampling_weights = torch.log(class_counts + 1.0)

        #train_sampler = SubsetRandomSampler(train_indices)
        train_sampler = ClassBalancedBatchSampler(
            class_to_indices=training_class_to_indices,
            classes_per_batch=classes_per_batch,
            samples_per_class=samples_per_class,
            num_batches=num_batches_per_epoch,
            with_replacement=True,
            class_sampling_weights=class_sampling_weights
        )

        test_sampler = SequentialSampler(test_indices)

        train_loader = DataLoader(deep_data, batch_sampler=train_sampler)
        test_loader = DataLoader(deep_data, batch_size=200, sampler=test_sampler, shuffle=False)

        model = SimpleMLP(len(data[0].flatten()), layers, len(unicode_count) ).to(device)

        # Manually scale parameters in-place using torch.no_grad() instead of scaling the input data.
        # This will avoid the need to scale the data when using the model in inference mode.
        # Scaling the model parameters instead of the data, leads to slightly lower classification
        # accuracy (both micro and macro accuracy)
        #with torch.no_grad():
        #    for param in model.parameters():
        #        param.data /= 255.0

        optimizer = optim.Adam(model.parameters(), lr=learning_rate)

        criterion = nn.CrossEntropyLoss()

        # Run training and testing
        for epoch in range(1, num_epoch):  # For each training epochs

            train_model(model, criterion, train_loader, optimizer, epoch, device)

            if (epoch%25 == 0) and (len(test_indices) > 0): # Only report test results infrequently to speed up training
                #test_model(model, criterion, test_loader, device, idx_to_unicode)
                test_model(model, criterion, test_loader, device)

            #if epoch == 200:
            #    test_model(model, criterion, test_loader, device, idx_to_unicode) # Write misclassified glyphs

        if args.param_filename:
            
            if num_fold != 1:
                param_filename = args.param_filename + '.fold=' + str(fold)
            else:
                param_filename = args.param_filename
            
            print('Writing model parameters to {} ... '.format(param_filename), end='', flush=True)

            serialize_mlp(param_filename, model, idx_to_unicode, endianness=endianness)

            print('done.', flush=True)

# Training loop
def train_model(model, criterion, loader, optimizer, epoch, device, jitter=0):

    model.train()

    info_buffer = ''
    num_batch = 0

    for batch_idx, (data, target) in enumerate(loader):

        if jitter > 0:
            # Quick test shows that it is faster to apply the jitter transformation prior to moving the data
            # to the GPU (ChatGPT suggests that this allows the CPU to take care of jittering while the GPU is
            # handling the back propagation work)
            data = jitter_batch_int(data, max_shift=0)

        # Double check transformations!
        #if batch_idx == 0:
        #    show_batch(data, nrows=2, ncols=4, cmap='gray')

        data, target = data.to(device), target.to(device)

        optimizer.zero_grad()
        output = model(data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()

        num_batch += 1

        if num_batch % 100 == 0:

            # Note that:
            #   len(loader.dataset) is the size of the *entire* dataset
            #   len(loader) is the number of batches in the current training set
            #   len(target) is the size of the current batch
            norm = len(loader)*len(target) # <-- The number of samples used for training

            print('\b'*len(info_buffer), end='')
            print(' '*len(info_buffer), end='')
            print('\b'*len(info_buffer), end='')

            info_buffer = f'\tTrain Epoch: {epoch} [{num_batch * len(target)}/{norm}] Loss: {loss.item():.4f}'

            print(info_buffer, end='', flush=True)

    print()

def test_model(model, criterion, loader, device, idx_to_unicode = None):
    
    model.eval()
    test_loss = 0
    correct = 0
    target_count = 0

    # Compute macro average 
    macro_count = dict()
    macro_correct = dict()

    # The previous version of this code was very slow due to data transfer from the GPU
    # to the CPU on every iteration. This is very inefficient!
    # Collect all of the predictions and *then* transfer from GPU->CPU.
    pred = []
    ground_truth = []

    debug_num_data = 0

    with torch.no_grad():
        for data, target in loader:

            debug_num_data += len(target)

            data, target = data.to(device), target.to(device)
            output = model(data)
            test_loss += criterion(output, target).item()
            pred.append( output.argmax(dim=1) ) # These are class indicies ([0, num_classes - 1]), *NOT* class labels!
            ground_truth.append( target ) # These are class indicies ([0, num_classes - 1]), *NOT* class labels!

    # Concatinate and move to the CPU
    pred = torch.cat(pred).cpu()
    ground_truth = torch.cat(ground_truth).cpu()

    # Compute the macro accuracy (thanks to Claude 4.5 Sonnet!)
    correct = (pred == ground_truth).sum().item()
    target_count = len(ground_truth)

    unique_targets = ground_truth.unique()
    macro_accuracy = 0

    for target in unique_targets:
        
        # Comparing a scalar with a tensor using == returns a tensor of True/False depending on per-element equality
        mask = ground_truth == target
        class_correct = (pred[mask] == ground_truth[mask]).sum().item() # Only compares elements corresponding to mask == True
        class_total = mask.sum().item()
        macro_accuracy += class_correct / class_total

        # DEBUG
        #if idx_to_unicode:
        #    print('{} -> {:.4}% = {}/{}'.format(chr(idx_to_unicode[target.item()]), 100.0*(class_correct / class_total), class_correct, class_total))

    macro_accuracy /= len(unique_targets)
    
    test_loss /= len(loader)
    accuracy = 100.0 * correct / target_count
    
    print(f"\tTest set: average loss {test_loss:.4f}; micro accuracy: {correct}/{target_count} ({accuracy:.2f}%); macro accuracy = {macro_accuracy*100.0:.2f}%")

    if idx_to_unicode:
    #if False:
        
        # The indicies of all the elements in pred that do NOT equal ground truth
        mismatchs = torch.nonzero(pred != ground_truth).squeeze()

        error_summary = dict() # (actual, pred) -> count

        for mm in mismatchs:

            key = ( ground_truth[mm].item(), pred[mm].item() )

            if key not in error_summary:
                error_summary[key] = 0
            
            error_summary[key] += 1
        
        for k, v in error_summary.items():

            # How many times did the actual unicode value appear in the test set?
            norm = (ground_truth == k[0]).sum().item()

            actual_unicode = idx_to_unicode[ k[0] ]
            pred_unicode = idx_to_unicode[  k[1] ]

            print('Predicted {} ({}) as {} ({}) {} times ({:.4}%)'.format(chr(actual_unicode), hex(actual_unicode), chr(pred_unicode), hex(pred_unicode), v, (100.0*v)/norm))

# See glyph_classifier.h for a list of model id values. The MLP classifier id value is 0x2
def serialize_mlp(output_parameter_file, model, idx_to_unicode, endianness = 'little', model_id = 0x2):

    try:
        fout = open(output_parameter_file, 'wb')
    except OSError:
        sys.stderr.write('Unable to open paramter file for writing\n')
        sys.exit(0)
    
    # Write the glyph classification algorithm id being used (see glyph_classifier.h)
    fout.write( model_id.to_bytes(4, endianness, signed=False) ) # The classification algorithm id as 4-byte unsigned int

    num_layers = 0

    for name, p in model.named_parameters():
        num_layers += 1

    num_layers /= 2 # There is a weight and bias for each layer, so divide by 2 to prevent overcounting

    buffer_len = len(idx_to_unicode)
    fout.write( buffer_len.to_bytes(4, endianness, signed=False) ) # The number of outputs as 4-byte unsigned int

    # The classes are unicode code points. Extract these values from the idx_to_unicode dictionary as 4-byte unsigned integers
    class_array = np.array([idx_to_unicode[idx] for idx in range(len(idx_to_unicode))])

    fout.write( class_array.astype('uint32').tobytes() ) # The array of output classes (each element is a 4-byte unsigned int)
    fout.write( int(num_layers).to_bytes(4, endianness, signed=False) ) # The number of MLP layers as 4-byte unsigned int

    for name, p in model.named_parameters():

        if 'weight' in name:

            p = p.detach().cpu().numpy()

            # Each layer is a matrix of shape(next num neuron, prev num neuron)
            next_len, prev_len = p.shape

            fout.write( next_len.to_bytes(4, endianness, signed=False) ) # The number of columns
            fout.write( prev_len.to_bytes(4, endianness, signed=False) ) # The number of rows

            # Default order of numpy arrays is C-contiguous order (row by row):
            fout.write( p.astype(np.float32).tobytes() ) # The matrix of layer weight (each element is an 4-byte float)

    for name, p in model.named_parameters():

        if 'bias' in name:

            p = p.detach().cpu().numpy()

            # Each layer is a vector with next num neuron elements
            next_len = p.shape[0]

            fout.write( next_len.to_bytes(4, endianness, signed=False) ) # The number of elements
            fout.write( p.astype(np.float32).tobytes() ) # The vector of bias values (each element is an 4-byte float)

    # Write the activation functions as un-terminated strings preceeded by a 1-byte string length
    str_buffer = 'relu'.encode('ascii')
    fout.write(len(str_buffer).to_bytes(1, endianness, signed=False))
    fout.write(str_buffer)

    str_buffer = 'softmax'.encode('ascii')
    fout.write(len(str_buffer).to_bytes(1, endianness, signed=False))
    fout.write(str_buffer)

    fout.close()

main()
