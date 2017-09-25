#include "glow/IR/Instrs.h"
#include "glow/IR/IR.h"

#include "glow/Network/Nodes.h"

#include <cassert>

using namespace glow;

// Helper methods that are used to print the instruction parameters.
namespace {
template <typename E> std::string listToString_impl(E v) {
  return std::to_string(v);
}

template <typename E, typename... Args>
std::string listToString_impl(E first, Args... args) {
  return std::to_string(first) + " " + listToString_impl(args...);
}

template <typename... Args> std::string listToString(Args... args) {
  return "[" + listToString_impl(args...) + "]";
}

template <typename E> std::string arrayRefToString(ArrayRef<E> list) {
  std::string sb = "[";
  for (int i = 0, e = list.size(); i < e; i++) {
    if (i) {
      sb += ", ";
    }
    sb += std::to_string(list[i]);
  }
  return sb + "]";
}
} // namespace

std::string ConvolutionInst::getExtraDesc() {
  return listToString(kernel_, stride_, pad_, depth_);
}

const char *PoolInst::getKindStr() {
  const char *names[] = {"max", "avg", nullptr};
  return names[(int)kind_];
}

std::string PoolInst::getExtraDesc() {
  std::string sb = getKindStr();
  return sb += " " + listToString(kernel_, stride_, pad_);
}

std::string FullyConnectedInst::getExtraDesc() { return listToString(depth_); }

std::string TransposeInst::getExtraDesc() {
  return arrayRefToString<unsigned>(shuffle_);
}

std::string ReshapeInst::getExtraDesc() {
  return arrayRefToString<size_t>(dims_);
}

std::string ConcatInst::getExtraDesc() {
  return "{ " + std::to_string(dim_) + " }";
}

std::string BatchNormalizationInst::getExtraDesc() {
  return listToString(channelIdx_, epsilon_, momentum_);
}

const char *ArithmeticInst::getKindStr() {
  const char *names[] = {"add", "mul", nullptr};
  return names[(int)kind_];
}

std::string ArithmeticInst::getExtraDesc() { return getKindStr(); }

const char *StaticVariable::getInitKindStr() {
  const char *names[] = {"extern", "broadcast", "xavier", nullptr};
  return names[(int)initKind_];
}

const char *StaticVariable::getShareKindStr() {
  const char *names[] = {"weight", "activation", nullptr};
  return names[(int)shareKind_];
}

std::string StaticVariable::getExtraDesc() {
  auto sp = ", ";
  auto r = getType()->asString() + sp + getShareKindStr();
  if (getInitKind() != InitKind::kExtern) {
    r += std::string(sp) + getInitKindStr() + sp + std::to_string(val_);
  }
  return r;
}

/// Check that the type of the first operand matches the type of the second
/// operand.
static void checkSameType(Instruction::Operand A, Instruction::Operand B) {
  assert(A.first->getType() == B.first->getType() && "Invalid type");
}

void CopyInst::verify() { checkSameType(getOperand(0), getOperand(1)); }
void ConvolutionInst::verify() {
  Value *dest = getOperand(0).first;
  Value *src = getOperand(1).first;
  Value *filter = getOperand(2).first;
  Value *bias = getOperand(3).first;
  (void)filter;
  (void)bias;

  ShapeNHWC idim = src->getType()->dims();
  ShapeNHWC odim = dest->getType()->dims();
  (void)odim;
  assert(idim.w >= kernel_ && idim.h >= kernel_ &&
         "buffer too small for selected stride");

  auto outSz =
      ConvNode::calculateOutputDims(idim.h, idim.w, pad_, kernel_, stride_);
  ShapeNHWC exp = ArrayRef<size_t>{idim.n, outSz.first, outSz.second, depth_};
  (void)exp;
  assert(exp == odim && "Invalid output dimensions");

  ArrayRef<size_t> filterDims = {depth_, kernel_, kernel_, idim.c};
  assert(filter->getType()->dims() == filterDims && "Invalid filter dims");

  ArrayRef<size_t> biasDims = {depth_};
  assert(bias->getType()->dims() == biasDims && "Invalid bias dims");
}

void PoolInst::verify() {
  Value *dest = getOperand(0).first;
  Value *src = getOperand(1).first;
  Value *srcXY = getOperand(2).first;
  (void)srcXY;
  ShapeNHWC idim = src->getType()->dims();
  ShapeNHWC odim = dest->getType()->dims();
  (void)odim;
  assert(idim.w >= kernel_ && idim.h >= kernel_ &&
         "buffer too small for selected stride");

  auto outSz =
      ConvNode::calculateOutputDims(idim.h, idim.w, pad_, kernel_, stride_);
  ShapeNHWC exp = ArrayRef<size_t>{idim.n, outSz.first, outSz.second, idim.c};
  (void)exp;
  assert(exp == odim && "Invalid output dimensions");

  // Allocate cache arrays that store the x and y coordinates of the incoming
  // gradient for each max element.
  if (kind_ == OpKind::kMax) {
    ArrayRef<size_t> exp = {idim.n, outSz.first, outSz.second, idim.c, 2};
    assert(srcXY->getType()->dims() == exp && "Invalid srcXY dims");
  }
}

void FullyConnectedInst::verify() {
  Value *dest = getOperand(0).first;
  Value *src = getOperand(1).first;
  Value *W = getOperand(2).first;
  Value *B = getOperand(3).first;
  (void)dest;
  (void)W;
  (void)B;
  auto idim = flattenCdr(src->dims());

  ArrayRef<size_t> exp = {idim.first, depth_};
  assert(dest->dims() == exp && "Invalid output shape");
  (void)exp;

  ArrayRef<size_t> expW = {depth_, idim.second};
  assert(W->dims() == expW && "Invalid output shape");
  (void)expW;

  ArrayRef<size_t> expB = {depth_};
  assert(B->dims() == expB && "Invalid output shape");
  (void)expB;
}

void ReluInst::verify() { checkSameType(getOperand(0), getOperand(1)); }
void SigmoidInst::verify() { checkSameType(getOperand(0), getOperand(1)); }
void TanhInst::verify() { checkSameType(getOperand(0), getOperand(1)); }
void SoftMaxInst::verify() { checkSameType(getOperand(0), getOperand(1)); }
void RegressionInst::verify() { checkSameType(getOperand(0), getOperand(1)); }

void ReshapeInst::verify() {
  assert(getOperand(0).first->getType()->size() ==
             getOperand(1).first->getType()->size() &&
         "Reshape into a different size");
}

void TransposeInst::verify() {
  auto *dest = getOperand(0).first;
  auto *src = getOperand(1).first;
  (void)dest;
  std::vector<size_t> shape;

  auto dims = src->dims();
  for (size_t i = 0; i < dims.size(); i++) {
    shape.push_back(dims[shuffle_[i]]);
  }

  assert(dest->dims() == ArrayRef<size_t>(shape) && "Invalid transpose dims");
}

void ConcatInst::verify() {
  assert(getNumOperands() > 1 && "Invalid number of operands");
  // The dimension of the first input.
  auto inDim = getOperand(1).first->dims();

  for (int i = 2, e = this->getNumOperands(); i < e; i++) {
    assert(getOperand(i).first->dims() == inDim && "Invalid input shape");
  }

  std::vector<size_t> shape(inDim.begin(), inDim.end());
  // We are stacking the tensors along a specific dimension. This means that we
  // increase the size of the tensor along this dimension.
  shape[dim_] *= getNumOperands() - 1;

  assert(getOperand(0).first->dims() == ArrayRef<size_t>(shape) &&
         "Invalid output shape");
}
void BatchNormalizationInst::verify() {
  checkSameType(getOperand(0), getOperand(1));

  // Figure out how many channels are in the tensor.
  size_t channels = getOperand(0).first->dims()[channelIdx_];

  ArrayRef<size_t> exp = {channels};
  assert(getOperand(2).first->getType()->dims() == exp && "Invalid bias dim");
  assert(getOperand(3).first->getType()->dims() == exp && "Invalid scale dim");
  assert(getOperand(4).first->getType()->dims() == exp && "Invalid mean dim");
  assert(getOperand(5).first->getType()->dims() == exp && "Invalid var dim");
}
void ArithmeticInst::verify() {
  checkSameType(getOperand(0), getOperand(1));
  checkSameType(getOperand(0), getOperand(2));
}