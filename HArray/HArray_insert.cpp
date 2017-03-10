
/*
# Copyright(C) 2010-2017 Vyacheslav Makoveychuk (email: slv709@gmail.com, skype: vyacheslavm81)
# This file is part of VyMa\Trie.
#
# VyMa\Trie is free software : you can redistribute it and / or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Vyma\Trie is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"
#include "HArray.h"

uint32 HArray::insert(uint32* key,
	uint32 keyLen,
	uint32 value)
{
	try
	{
		keyLen >>= 2; //in 4 bytes

		uint32 maxSafeShort = MAX_SAFE_SHORT - keyLen;

		//create new page =======================================================================================
		if (!pContentPages[(lastContentOffset + keyLen + ValueLen) >> 16])
		{
			pContentPages[ContentPagesCount++] = new ContentPage();

			if (ContentPagesCount == ContentPagesSize)
			{
				reallocateContentPages();
			}
		}

		//insert value ==========================================================================================
		uint32 keyOffset = 0;
		uint32 contentOffset = 0;

		uint32 headerOffset;

		if (!normalizeFunc)
		{
			headerOffset = key[0] >> HeaderBits;
		}
		else
		{
			headerOffset = (*normalizeFunc)(key) >> HeaderBits;
		}

		ContentCell* pHeaderCell = &pHeader[headerOffset];

		ContentPage* pContentPage;
		uint32 contentIndex;

		ContentCell* pContentCell;

		uint32* pSetLastContentOffset;

		if (pHeaderCell->Type == EMPTY_TYPE)
		{
#ifndef _RELEASE
			tempValues[SHORT_WAY_STAT]++;
#endif
			//insert key in free slot
			pHeaderCell->Type = HEADER_JUMP_TYPE;

			if (tailReleasedContentOffsets[keyLen])
			{
				uint32 startContentOffset = tailReleasedContentOffsets[keyLen];

				pHeaderCell->Value = startContentOffset;

				ContentPage* pContentPage = pContentPages[startContentOffset >> 16];
				uint32 contentIndex = startContentOffset & 0xFFFF;

				pContentPage->pContent[contentIndex].Type = (ONLY_CONTENT_TYPE + keyLen);
				tailReleasedContentOffsets[keyLen] = pContentPage->pContent[contentIndex].Value;

				countReleasedContentCells -= (keyLen + ValueLen);

				//fill key
				for (; keyOffset < keyLen; contentIndex++, keyOffset++)
				{
					pContentPage->pContent[contentIndex].Value = key[keyOffset];
				}

				pContentPage->pContent[contentIndex].Type = VALUE_TYPE;
				pContentPage->pContent[contentIndex].Value = value;

				return 0;
			}
			else
			{
				ContentPage* pContentPage = pContentPages[lastContentOffset >> 16];
				uint32 contentIndex = lastContentOffset & 0xFFFF;

				if (contentIndex > maxSafeShort) //content in one page
				{
					//release content cells
					uint32 spaceLen = MAX_SHORT - contentIndex - 1;

					pContentPage->pContent[contentIndex].Value = tailReleasedContentOffsets[spaceLen];

					tailReleasedContentOffsets[spaceLen] = lastContentOffset;

					countReleasedContentCells += (spaceLen + ValueLen);

					//move to next page
					uint32 page = (lastContentOffset >> 16) + 1;

					pContentPage = pContentPages[page];

					lastContentOffset = (page << 16);

					contentIndex = 0;
				}

				pHeaderCell->Value = lastContentOffset;

				pContentPage->pContent[contentIndex].Type = (ONLY_CONTENT_TYPE + keyLen);

				//fill key
				for (; keyOffset < keyLen; contentIndex++, keyOffset++, lastContentOffset++)
				{
					pContentPage->pContent[contentIndex].Value = key[keyOffset];
				}

				pContentPage->pContent[contentIndex].Type = VALUE_TYPE;
				pContentPage->pContent[contentIndex].Value = value;

				lastContentOffset++;

				return 0;
			}
		}
		else if (pHeaderCell->Type == HEADER_JUMP_TYPE)
		{
			contentOffset = pHeaderCell->Value;
		}
		else //content cell injected in header
		{
			pContentCell = pHeaderCell;

			goto HEADER_CONTENT_CELL;
		}

#ifndef _RELEASE
		tempValues[LONG_WAY_STAT]++;
#endif

		//TWO KEYS =============================================================================================
	NEXT_KEY_PART:

		pContentPage = pContentPages[contentOffset >> 16];
		contentIndex = contentOffset & 0xFFFF;

		pContentCell = &pContentPage->pContent[contentIndex];

		if (pContentCell->Type >= ONLY_CONTENT_TYPE) //ONLY CONTENT =========================================================================
		{
			uint32 originKeyLen = keyOffset + pContentCell->Type - ONLY_CONTENT_TYPE;

			for (; keyOffset < originKeyLen; contentOffset++, contentIndex++, keyOffset++)
			{
				ContentCell& contentCell = pContentPage->pContent[contentIndex];

				//key less than original, insert
				if (keyOffset == keyLen)
				{
					VarCell* pVarCell;

					if (countReleasedVarCells)
					{
						uint32 varOffset = tailReleasedVarOffset;

						pVarCell = &pVarPages[varOffset >> 16]->pVar[varOffset & 0xFFFF];

						tailReleasedVarOffset = pVarCell->ValueContCell.Value;

						countReleasedVarCells--;
					}
					else
					{
						VarPage* pVarPage = pVarPages[lastVarOffset >> 16];
						if (!pVarPage)
						{
							pVarPage = new VarPage();
							pVarPages[VarPagesCount++] = pVarPage;

							if (VarPagesCount == VarPagesSize)
							{
								reallocateVarPages();
							}
						}

						pVarCell = &pVarPage->pVar[lastVarOffset & 0xFFFF];
					}

					pVarCell->ContCell.Type = CURRENT_VALUE_TYPE;
					pVarCell->ContCell.Value = contentCell.Value;

					pVarCell->ValueContCell.Type = VALUE_TYPE;
					pVarCell->ValueContCell.Value = value;

					contentCell.Type = VAR_TYPE;
					contentCell.Value = lastVarOffset++;

					//set rest of key and value
					for (contentIndex++, keyOffset++; keyOffset < originKeyLen; contentIndex++, keyOffset++)
					{
						pContentPage->pContent[contentIndex].Type = CURRENT_VALUE_TYPE;
					}

					pContentPage->pContent[contentIndex].Type = VALUE_TYPE;

					return 0;
				}
				else if (contentCell.Value != key[keyOffset])
				{
#ifndef _RELEASE
					tempValues[CONTENT_BRANCH_STAT]++;
#endif

					//create branch
					contentCell.Type = MIN_BRANCH_TYPE1 + 1;

					//get free branch cell
					BranchCell* pBranchCell;
					if (countReleasedBranchCells)
					{
						uint32 branchOffset = tailReleasedBranchOffset;

						pBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

						tailReleasedBranchOffset = pBranchCell->Values[0];

						countReleasedBranchCells--;

						pBranchCell->Values[0] = contentCell.Value;
						contentCell.Value = branchOffset;
					}
					else
					{
						BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
						if (!pBranchPage)
						{
							pBranchPage = new BranchPage();
							pBranchPages[BranchPagesCount++] = pBranchPage;

							if (BranchPagesCount == BranchPagesSize)
							{
								reallocateBranchPages();
							}
						}

						pBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];

						pBranchCell->Values[0] = contentCell.Value;
						contentCell.Value = lastBranchOffset++;
					}

					pBranchCell->Offsets[0] = contentOffset + 1;

					pBranchCell->Values[1] = key[keyOffset];
					pSetLastContentOffset = &pBranchCell->Offsets[1];

					if ((keyOffset + 1) < originKeyLen)
					{
						pContentPage->pContent[contentIndex + 1].Type = (ONLY_CONTENT_TYPE + originKeyLen - keyOffset - 1);
					}

					goto FILL_KEY;
				}
				else
				{
					contentCell.Type = CURRENT_VALUE_TYPE; //reset to current value
				}
			}

			if (keyLen > originKeyLen) //key more than original
			{
				VarCell* pVarCell;

				if (countReleasedVarCells)
				{
					uint32 varOffset = tailReleasedVarOffset;

					pVarCell = &pVarPages[varOffset >> 16]->pVar[varOffset & 0xFFFF];

					tailReleasedVarOffset = pVarCell->ValueContCell.Value;

					countReleasedVarCells--;
				}
				else
				{
					VarPage* pVarPage = pVarPages[lastVarOffset >> 16];
					if (!pVarPage)
					{
						pVarPage = new VarPage();
						pVarPages[VarPagesCount++] = pVarPage;

						if (VarPagesCount == VarPagesSize)
						{
							reallocateVarPages();
						}
					}

					pVarCell = &pVarPage->pVar[lastVarOffset & 0xFFFF];
				}

				ContentCell& contentCell = pContentPage->pContent[contentIndex];

				pVarCell->ValueContCell.Type = VALUE_TYPE;
				pVarCell->ValueContCell.Value = contentCell.Value;

				pVarCell->ContCell.Type = CONTINUE_VAR_TYPE;
				pSetLastContentOffset = &pVarCell->ContCell.Value;

				contentCell.Type = VAR_TYPE;
				contentCell.Value = lastVarOffset++;

				goto FILL_KEY2;
			}
			else //key is exists, update
			{
				ContentCell& contentCell = pContentPage->pContent[contentIndex];

				contentCell.Value = value;

				return 0;
			}

			return 0;
		}

		pContentCell = &pContentPage->pContent[contentIndex];

		if (pContentCell->Type == VAR_TYPE) //VAR =====================================================================
		{
			VarPage* pVarPage = pVarPages[pContentCell->Value >> 16];
			VarCell& varCell = pVarPage->pVar[pContentCell->Value & 0xFFFF];

			if (keyOffset < keyLen)
			{
				pContentCell = &varCell.ContCell;

				if (pContentCell->Type == CONTINUE_VAR_TYPE) //CONTINUE VAR =====================================================================
				{
					contentOffset = pContentCell->Value;

					goto NEXT_KEY_PART;
				}
			}
			else
			{
				//update existing value
				varCell.ValueContCell.Type = VALUE_TYPE;
				varCell.ValueContCell.Value = value;

				return 0;
			}
		}
		else if (pContentCell->Type == VALUE_TYPE) //update existing value
		{
			if (keyOffset < keyLen)
			{
				VarCell* pVarCell;

				if (countReleasedVarCells)
				{
					uint32 varOffset = tailReleasedVarOffset;

					pVarCell = &pVarPages[varOffset >> 16]->pVar[varOffset & 0xFFFF];

					tailReleasedVarOffset = pVarCell->ValueContCell.Value;

					countReleasedVarCells--;
				}
				else
				{
					VarPage* pVarPage = pVarPages[lastVarOffset >> 16];
					if (!pVarPage)
					{
						pVarPage = new VarPage();
						pVarPages[VarPagesCount++] = pVarPage;

						if (VarPagesCount == VarPagesSize)
						{
							reallocateVarPages();
						}
					}

					pVarCell = &pVarPage->pVar[lastVarOffset & 0xFFFF];
				}

				ContentCell& contentCell = pContentPage->pContent[contentIndex];

				pVarCell->ValueContCell.Type = VALUE_TYPE;
				pVarCell->ValueContCell.Value = contentCell.Value;

				pVarCell->ContCell.Type = CONTINUE_VAR_TYPE;
				pSetLastContentOffset = &pVarCell->ContCell.Value;

				contentCell.Type = VAR_TYPE;
				contentCell.Value = lastVarOffset++;

				goto FILL_KEY2;
			}
			else
			{
				pContentCell->Value = value;

				return 0;
			}
		}
		else if (keyOffset == keyLen) //STOP =====================================================================
		{
			VarCell* pVarCell;

			if (countReleasedVarCells)
			{
				uint32 varOffset = tailReleasedVarOffset;

				pVarCell = &pVarPages[varOffset >> 16]->pVar[varOffset & 0xFFFF];

				tailReleasedVarOffset = pVarCell->ValueContCell.Value;

				countReleasedVarCells--;
			}
			else
			{
				VarPage* pVarPage = pVarPages[lastVarOffset >> 16];
				if (!pVarPage)
				{
					pVarPage = new VarPage();
					pVarPages[VarPagesCount++] = pVarPage;

					if (VarPagesCount == VarPagesSize)
					{
						reallocateVarPages();
					}
				}

				pVarCell = &pVarPage->pVar[lastVarOffset & 0xFFFF];
			}

			ContentCell& contentCell = pContentPage->pContent[contentIndex];

			pVarCell->ContCell.Type = contentCell.Type;
			pVarCell->ContCell.Value = contentCell.Value;

			pVarCell->ValueContCell.Type = VALUE_TYPE;
			pVarCell->ValueContCell.Value = value;

			contentCell.Type = VAR_TYPE;
			contentCell.Value = lastVarOffset++;

			return 0;
		}

	HEADER_CONTENT_CELL:

		uint32 keyValue;

		keyValue = key[keyOffset];

		if (pContentCell->Type <= MAX_BRANCH_TYPE1) //BRANCH =====================================================================
		{
			BranchPage* pBranchPage = pBranchPages[pContentCell->Value >> 16];
			BranchCell& branchCell = pBranchPage->pBranch[pContentCell->Value & 0xFFFF];

			//try find value in the list
			for (uint32 i = 0; i < pContentCell->Type; i++)
			{
				if (branchCell.Values[i] == keyValue)
				{
					contentOffset = branchCell.Offsets[i];
					keyOffset++;

					goto NEXT_KEY_PART;
				}
			}

			//create value in branch
			if (pContentCell->Type < MAX_BRANCH_TYPE1)
			{
				branchCell.Values[pContentCell->Type] = keyValue;
				pSetLastContentOffset = &branchCell.Offsets[pContentCell->Type];

				pContentCell->Type++;

				goto FILL_KEY;
			}
			else
			{
#ifndef _RELEASE
				tempValues[CONTENT_BRANCH_STAT]--;
				tempValues[MOVES_LEVEL1_STAT]++;
#endif

				//EXTRACT BRANCH AND CREATE BLOCK =========================================================
				uchar8 idxKeyValue = 0;
				uchar8 currContentCellType = MIN_BLOCK_TYPE;

				const ushort16 countCell = BRANCH_ENGINE_SIZE + 1;

			EXTRACT_BRANCH:

				BlockCell blockCells[countCell];
				uchar8 indexes[countCell];

				for (uint32 i = 0; i < MAX_BRANCH_TYPE1; i++)
				{
					BlockCell& currBlockCell = blockCells[i];
					currBlockCell.Type = CURRENT_VALUE_TYPE;
					currBlockCell.Offset = branchCell.Offsets[i];

					uint32& value = branchCell.Values[i];
					currBlockCell.ValueOrOffset = value;

					indexes[i] = ((value << idxKeyValue) >> BLOCK_ENGINE_SHIFT); //((uchar8*)&value)[idxKeyValue];
				}

				//add current value
				BlockCell& currBlockCell4 = blockCells[BRANCH_ENGINE_SIZE];
				currBlockCell4.Type = CURRENT_VALUE_TYPE;
				//currBlockCell4.Offset = lastContentOffset; //will be set later
				currBlockCell4.ValueOrOffset = keyValue;

				indexes[BRANCH_ENGINE_SIZE] = ((keyValue << idxKeyValue) >> BLOCK_ENGINE_SHIFT);

				//clear map
				uchar8 mapIndexes[BLOCK_ENGINE_SIZE];
				for (uint32 i = 0; i < countCell; i++)
				{
					mapIndexes[indexes[i]] = 0;
				}

				//fill map
				for (uint32 i = 0; i < countCell; i++)
				{
					uchar8& countIndexes = mapIndexes[indexes[i]];
					countIndexes++;

					if (countIndexes > BRANCH_ENGINE_SIZE)
					{
						idxKeyValue += BLOCK_ENGINE_STEP;
						currContentCellType++;
						goto EXTRACT_BRANCH; //use another byte
					}
				}

				//release branch cell
				memset(&branchCell, 0, sizeof(BranchCell));

				branchCell.Values[0] = tailReleasedBranchOffset;

				tailReleasedBranchOffset = pContentCell->Value;

				countReleasedBranchCells++;

				//get free block
				uint32 startBlockOffset;

				if (countReleasedBlockCells)
				{
					startBlockOffset = tailReleasedBlockOffset;

					tailReleasedBlockOffset = pBlockPages[startBlockOffset >> 16]->pBlock[startBlockOffset & 0xFFFF].Offset;

					countReleasedBlockCells -= BLOCK_ENGINE_SIZE;
				}
				else
				{
					startBlockOffset = lastBlockOffset;

					uint32 maxLastBlockOffset = startBlockOffset + BLOCK_ENGINE_SIZE * 2;
					if (!pBlockPages[maxLastBlockOffset >> 16])
					{
						pBlockPages[BlockPagesCount++] = new BlockPage();

						if (BlockPagesCount == BlockPagesSize)
						{
							reallocateBlockPages();
						}
					}

					lastBlockOffset += BLOCK_ENGINE_SIZE;
				}

				//fill block
				pContentCell->Type = currContentCellType;
				pContentCell->Value = startBlockOffset;

				for (uint32 i = 0; i < countCell; i++)
				{
					uchar8 idx = indexes[i];
					uint32 offset = startBlockOffset + idx;

					BlockPage* pBlockPage = pBlockPages[offset >> 16];
					BlockCell& currBlockCell = pBlockPage->pBlock[offset & 0xFFFF];

					uchar8 count = mapIndexes[idx];
					if (count == 1) //one value in block cell
					{
						currBlockCell = blockCells[i];

						if (i == BRANCH_ENGINE_SIZE) //last cell
						{
							pSetLastContentOffset = &currBlockCell.Offset;
						}
					}
					else if (count <= BRANCH_ENGINE_SIZE) //create branch cell
					{
						if (currBlockCell.Type == 0) //create branch
						{
							//get free branch cell
							BranchCell* pCurrBranchCell;
							if (countReleasedBranchCells)
							{
								uint32 branchOffset = tailReleasedBranchOffset;

								pCurrBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

								tailReleasedBranchOffset = pCurrBranchCell->Values[0];

								countReleasedBranchCells--;

								currBlockCell.Offset = branchOffset;
							}
							else
							{
								BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
								if (!pBranchPage)
								{
									pBranchPage = new BranchPage();
									pBranchPages[BranchPagesCount++] = pBranchPage;

									if (BranchPagesCount == BranchPagesSize)
									{
										reallocateBranchPages();
									}
								}

								pCurrBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];
								currBlockCell.Offset = lastBranchOffset++;
							}

							currBlockCell.Type = MIN_BRANCH_TYPE1;

							pCurrBranchCell->Values[0] = blockCells[i].ValueOrOffset;

							if (i < BRANCH_ENGINE_SIZE) //last cell ?
							{
								pCurrBranchCell->Offsets[0] = blockCells[i].Offset;
							}
							else
							{
								pSetLastContentOffset = &pCurrBranchCell->Offsets[0];
							}
						}
						else //types 1..4
						{
							BranchPage* pBranchPage = pBranchPages[currBlockCell.Offset >> 16];
							BranchCell& currBranchCell = pBranchPage->pBranch[currBlockCell.Offset & 0xFFFF];

							currBranchCell.Values[currBlockCell.Type] = blockCells[i].ValueOrOffset;

							if (i < BRANCH_ENGINE_SIZE) //last cell ?
							{
								currBranchCell.Offsets[currBlockCell.Type] = blockCells[i].Offset;
							}
							else
							{
								pSetLastContentOffset = &currBranchCell.Offsets[currBlockCell.Type];
							}

							currBlockCell.Type++;
						}
					}
					else
					{
						printf("FAIL STATE.");
					}
				}

				goto FILL_KEY;
			}
		}
		else if (pContentCell->Type <= MAX_BLOCK_TYPE) //VALUE IN BLOCK ===================================================================
		{
			uint32 level = 1;

			uchar8 idxKeyValue = (pContentCell->Type - MIN_BLOCK_TYPE) * BLOCK_ENGINE_STEP;

			uint32 startOffset = pContentCell->Value;

		NEXT_BLOCK:
			uint32 subOffset = ((keyValue << idxKeyValue) >> BLOCK_ENGINE_SHIFT);
			uint32 blockOffset = startOffset + subOffset;

			BlockCell& blockCell = pBlockPages[blockOffset >> 16]->pBlock[blockOffset & 0xFFFF];

			uchar8& blockCellType = blockCell.Type;

			if (blockCellType == EMPTY_TYPE) //release cell, fill
			{
				blockCellType = CURRENT_VALUE_TYPE;
				blockCell.ValueOrOffset = keyValue;
				pSetLastContentOffset = &blockCell.Offset;

				goto FILL_KEY;
			}
			else //block cell
			{
				if (blockCellType == CURRENT_VALUE_TYPE) //current value
				{
					if (blockCell.ValueOrOffset == keyValue) //value is exists
					{
						contentOffset = blockCell.Offset;
						keyOffset++;

						goto NEXT_KEY_PART;
					}
					else //create branch with two values
					{
						blockCellType = MIN_BRANCH_TYPE1 + 1;

						//get free branch cell
						BranchCell* pBranchCell;
						if (countReleasedBranchCells)
						{
							uint32 branchOffset = tailReleasedBranchOffset;

							pBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

							tailReleasedBranchOffset = pBranchCell->Values[0];

							countReleasedBranchCells--;

							pBranchCell->Offsets[0] = blockCell.Offset;
							blockCell.Offset = branchOffset;
						}
						else
						{
							BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
							if (!pBranchPage)
							{
								pBranchPage = new BranchPage();
								pBranchPages[BranchPagesCount++] = pBranchPage;

								if (BranchPagesCount == BranchPagesSize)
								{
									reallocateBranchPages();
								}
							}

							pBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];

							pBranchCell->Offsets[0] = blockCell.Offset;
							blockCell.Offset = lastBranchOffset++;
						}

						pBranchCell->Values[0] = blockCell.ValueOrOffset;

						pBranchCell->Values[1] = keyValue;
						pSetLastContentOffset = &pBranchCell->Offsets[1];

						goto FILL_KEY;
					}
				}
				else if (blockCellType <= MAX_BRANCH_TYPE1) //branch cell 1
				{
					BranchPage* pBranchPage1 = pBranchPages[blockCell.Offset >> 16];
					BranchCell& branchCell1 = pBranchPage1->pBranch[blockCell.Offset & 0xFFFF];

					//first branch
					//try find value in the list
					for (uint32 i = 0; i < blockCellType; i++)
					{
						if (branchCell1.Values[i] == keyValue)
						{
							contentOffset = branchCell1.Offsets[i];
							keyOffset++;

							goto NEXT_KEY_PART;
						}
					}

					if (blockCellType < MAX_BRANCH_TYPE1)
					{
						branchCell1.Values[blockCellType] = keyValue;
						pSetLastContentOffset = &branchCell1.Offsets[blockCellType];

						blockCellType++;

						goto FILL_KEY;
					}

					//create second branch
					blockCellType = MIN_BRANCH_TYPE2;

					//get free branch cell
					BranchCell* pBranchCell;
					if (countReleasedBranchCells)
					{
						uint32 branchOffset = tailReleasedBranchOffset;

						pBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

						tailReleasedBranchOffset = pBranchCell->Values[0];

						countReleasedBranchCells--;

						blockCell.ValueOrOffset = branchOffset;
					}
					else
					{
						BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
						if (!pBranchPage)
						{
							pBranchPage = new BranchPage();
							pBranchPages[BranchPagesCount++] = pBranchPage;

							if (BranchPagesCount == BranchPagesSize)
							{
								reallocateBranchPages();
							}
						}

						pBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];

						blockCell.ValueOrOffset = lastBranchOffset++;
					}

					pBranchCell->Values[0] = keyValue;
					pSetLastContentOffset = &pBranchCell->Offsets[0];

					goto FILL_KEY;
				}
				else if (blockCellType <= MAX_BRANCH_TYPE2) //branch cell 2
				{
					BranchPage* pBranchPage1 = pBranchPages[blockCell.Offset >> 16];
					BranchCell& branchCell1 = pBranchPage1->pBranch[blockCell.Offset & 0xFFFF];

					//first branch
					//try find value in the list
					for (uint32 i = 0; i < BRANCH_ENGINE_SIZE; i++)
					{
						if (branchCell1.Values[i] == keyValue)
						{
							contentOffset = branchCell1.Offsets[i];
							keyOffset++;

							goto NEXT_KEY_PART;
						}
					}

					//second branch
					BranchPage* pBranchPage2 = pBranchPages[blockCell.ValueOrOffset >> 16];
					BranchCell& branchCell2 = pBranchPage2->pBranch[blockCell.ValueOrOffset & 0xFFFF];

					//try find value in the list
					uint32 countValues = blockCellType - MAX_BRANCH_TYPE1;

					for (uchar8 i = 0; i < countValues; i++)
					{
						if (branchCell2.Values[i] == keyValue)
						{
							contentOffset = branchCell2.Offsets[i];
							keyOffset++;

							goto NEXT_KEY_PART;
						}
					}

					//create value in branch
					if (blockCellType < MAX_BRANCH_TYPE2) //add to branch value
					{
						branchCell2.Values[countValues] = keyValue;
						pSetLastContentOffset = &branchCell2.Offsets[countValues];

						blockCellType++;

						goto FILL_KEY;
					}
					else
					{
						//CREATE NEXT BLOCK ==========================================================
#ifndef _RELEASE
						switch (level)
						{
						case 1:
							tempValues[MOVES_LEVEL1_STAT]++;
							break;
						case 2:
							tempValues[MOVES_LEVEL2_STAT]++;
							break;
						case 3:
							tempValues[MOVES_LEVEL3_STAT]++;
							break;
						case 4:
							tempValues[MOVES_LEVEL4_STAT]++;
							break;
						case 5:
							tempValues[MOVES_LEVEL5_STAT]++;
							break;
						case 6:
							tempValues[MOVES_LEVEL6_STAT]++;
							break;
						case 7:
							tempValues[MOVES_LEVEL7_STAT]++;
							break;
						case 8:
							tempValues[MOVES_LEVEL8_STAT]++;
							break;
						default:
							break;
						}
#endif

						//if(level == 3) //max depth
						//{
						//	if(allocateHeaderBlock(keyValue, lastContentOffset, pContentCell))
						//	{
						//		return 333; //goto FILL_KEY2;
						//	}
						//}

						const ushort16 branchesSize = BRANCH_ENGINE_SIZE * 2;
						const ushort16 countCell = branchesSize + 1;

						BlockCell blockCells[countCell];
						uchar8 indexes[countCell];

					EXTRACT_BRANCH2:
						idxKeyValue += BLOCK_ENGINE_STEP;
						if (idxKeyValue > BLOCK_ENGINE_SHIFT)
							idxKeyValue = 0;

						//idxKeyValue &= BLOCK_ENGINE_MASK;

						uchar8 j = 0;

						//first branch
						for (uint32 i = 0; i < BRANCH_ENGINE_SIZE; i++, j++)
						{
							BlockCell& currBlockCell = blockCells[j];
							currBlockCell.Type = CURRENT_VALUE_TYPE;
							currBlockCell.Offset = branchCell1.Offsets[i];

							uint32& value = branchCell1.Values[i];
							currBlockCell.ValueOrOffset = value;

							indexes[j] = ((value << idxKeyValue) >> BLOCK_ENGINE_SHIFT);
						}

						//second branch
						for (uint32 i = 0; i < BRANCH_ENGINE_SIZE; i++, j++)
						{
							BlockCell& currBlockCell = blockCells[j];
							currBlockCell.Type = CURRENT_VALUE_TYPE;
							currBlockCell.Offset = branchCell2.Offsets[i];

							uint32& value = branchCell2.Values[i];
							currBlockCell.ValueOrOffset = value;

							indexes[j] = ((value << idxKeyValue) >> BLOCK_ENGINE_SHIFT);
						}

						//add current value
						BlockCell& currBlockCell8 = blockCells[branchesSize];
						currBlockCell8.Type = CURRENT_VALUE_TYPE;
						//currBlockCell8.Offset = lastContentOffset; //will be set later
						currBlockCell8.ValueOrOffset = keyValue;

						indexes[branchesSize] = ((keyValue << idxKeyValue) >> BLOCK_ENGINE_SHIFT);

						//clear map
						uchar8 mapIndexes[BLOCK_ENGINE_SIZE];
						for (uchar8 i = 0; i < countCell; i++)
						{
							mapIndexes[indexes[i]] = 0;
						}

						//fill map
						for (uint32 i = 0; i < countCell; i++)
						{
							uchar8& countIndexes = mapIndexes[indexes[i]];
							countIndexes++;

							if (countIndexes > branchesSize)
							{
								goto EXTRACT_BRANCH2; //use another byte
							}
						}

						//release branch cell 1
						memset(&branchCell1, 0, sizeof(BranchCell));

						branchCell1.Values[0] = tailReleasedBranchOffset;

						tailReleasedBranchOffset = blockCell.Offset;
						blockCell.Offset = 0;

						countReleasedBranchCells++;

						//release branch cell 2
						memset(&branchCell2, 0, sizeof(BranchCell));

						branchCell2.Values[0] = tailReleasedBranchOffset;

						tailReleasedBranchOffset = blockCell.ValueOrOffset;
						blockCell.ValueOrOffset = 0;

						countReleasedBranchCells++;

						//get free block
						uint32 startBlockOffset;

						if (countReleasedBlockCells)
						{
							startBlockOffset = tailReleasedBlockOffset;

							tailReleasedBlockOffset = pBlockPages[startBlockOffset >> 16]->pBlock[startBlockOffset & 0xFFFF].Offset;

							countReleasedBlockCells -= BLOCK_ENGINE_SIZE;
						}
						else
						{
							startBlockOffset = lastBlockOffset;

							uint32 maxLastBlockOffset = startBlockOffset + BLOCK_ENGINE_SIZE * 2;
							if (!pBlockPages[maxLastBlockOffset >> 16])
							{
								pBlockPages[BlockPagesCount++] = new BlockPage();

								if (BlockPagesCount == BlockPagesSize)
								{
									reallocateBlockPages();
								}
							}

							lastBlockOffset += BLOCK_ENGINE_SIZE;
						}

						//fill block
						blockCellType = MIN_BLOCK_TYPE + (idxKeyValue / BLOCK_ENGINE_STEP);
						blockCell.Offset = startBlockOffset;

						for (uint32 i = 0; i < countCell; i++)
						{
							uchar8 idx = indexes[i];
							uint32 offset = startBlockOffset + idx;

							BlockPage* pBlockPage = pBlockPages[offset >> 16];
							BlockCell& currBlockCell = pBlockPage->pBlock[offset & 0xFFFF];

							uchar8 count = mapIndexes[idx];
							if (count == 1) //one value in block cell
							{
								currBlockCell = blockCells[i];

								if (i == branchesSize) //last cell
								{
									pSetLastContentOffset = &currBlockCell.Offset;
								}
							}
							else
							{
								if (currBlockCell.Type == 0) //create branch
								{
									//get free branch cell
									BranchCell* pCurrBranchCell;
									if (countReleasedBranchCells)
									{
										uint32 branchOffset = tailReleasedBranchOffset;

										pCurrBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

										tailReleasedBranchOffset = pCurrBranchCell->Values[0];

										countReleasedBranchCells--;

										currBlockCell.Offset = branchOffset;
									}
									else
									{
										BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
										if (!pBranchPage)
										{
											pBranchPage = new BranchPage();
											pBranchPages[BranchPagesCount++] = pBranchPage;

											if (BranchPagesCount == BranchPagesSize)
											{
												reallocateBranchPages();
											}
										}

										pCurrBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];

										currBlockCell.Offset = lastBranchOffset++;
									}

									currBlockCell.Type = MIN_BRANCH_TYPE1;

									pCurrBranchCell->Values[0] = blockCells[i].ValueOrOffset;

									if (i < branchesSize) //last cell ?
									{
										pCurrBranchCell->Offsets[0] = blockCells[i].Offset;
									}
									else
									{
										pSetLastContentOffset = &pCurrBranchCell->Offsets[0];
									}
								}
								else if (currBlockCell.Type < MAX_BRANCH_TYPE1)
								{
									BranchPage* pBranchPage = pBranchPages[currBlockCell.Offset >> 16];
									BranchCell& currBranchCell = pBranchPage->pBranch[currBlockCell.Offset & 0xFFFF];

									currBranchCell.Values[currBlockCell.Type] = blockCells[i].ValueOrOffset;

									if (i < branchesSize) //last cell ?
									{
										currBranchCell.Offsets[currBlockCell.Type] = blockCells[i].Offset;
									}
									else
									{
										pSetLastContentOffset = &currBranchCell.Offsets[currBlockCell.Type];
									}

									currBlockCell.Type++;
								}
								else if (currBlockCell.Type == MAX_BRANCH_TYPE1)
								{
									//get free branch cell
									BranchCell* pCurrBranchCell;
									if (countReleasedBranchCells)
									{
										uint32 branchOffset = tailReleasedBranchOffset;

										pCurrBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

										tailReleasedBranchOffset = pCurrBranchCell->Values[0];

										countReleasedBranchCells--;

										currBlockCell.ValueOrOffset = branchOffset;
									}
									else
									{
										BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
										if (!pBranchPage)
										{
											pBranchPage = new BranchPage();
											pBranchPages[BranchPagesCount++] = pBranchPage;

											if (BranchPagesCount == BranchPagesSize)
											{
												reallocateBranchPages();
											}
										}

										pCurrBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];

										currBlockCell.ValueOrOffset = lastBranchOffset++;
									}

									currBlockCell.Type = MIN_BRANCH_TYPE2;

									pCurrBranchCell->Values[0] = blockCells[i].ValueOrOffset;

									if (i < branchesSize) //last cell ?
									{
										pCurrBranchCell->Offsets[0] = blockCells[i].Offset;
									}
									else
									{
										pSetLastContentOffset = &pCurrBranchCell->Offsets[0];
									}
								}
								else
								{
									uint32 countValues = currBlockCell.Type - MAX_BRANCH_TYPE1;

									BranchPage* pBranchPage = pBranchPages[currBlockCell.ValueOrOffset >> 16];
									BranchCell& currBranchCell = pBranchPage->pBranch[currBlockCell.ValueOrOffset & 0xFFFF];

									currBranchCell.Values[countValues] = blockCells[i].ValueOrOffset;

									if (i < branchesSize) //last cell ?
									{
										currBranchCell.Offsets[countValues] = blockCells[i].Offset;
									}
									else
									{
										pSetLastContentOffset = &currBranchCell.Offsets[countValues];
									}

									currBlockCell.Type++;
								}
							}
						}

						goto FILL_KEY;
					}
				}
				else if (blockCell.Type <= MAX_BLOCK_TYPE)
				{
					//go to block
					idxKeyValue = (blockCell.Type - MIN_BLOCK_TYPE) * BLOCK_ENGINE_STEP;
					startOffset = blockCell.Offset;

					level++;

					goto NEXT_BLOCK;
				}
			}
		}
		else if (pContentCell->Type == CURRENT_VALUE_TYPE) //PART OF KEY =========================================================================
		{
			if (pContentCell->Value == keyValue)
			{
				contentOffset++;
				keyOffset++;

				goto NEXT_KEY_PART;
			}
			else //create branch
			{
				pContentCell->Type = MIN_BRANCH_TYPE1 + 1;

				//get free branch cell
				BranchCell* pBranchCell;
				if (countReleasedBranchCells)
				{
					uint32 branchOffset = tailReleasedBranchOffset;

					pBranchCell = &pBranchPages[branchOffset >> 16]->pBranch[branchOffset & 0xFFFF];

					tailReleasedBranchOffset = pBranchCell->Values[0];

					countReleasedBranchCells--;

					pBranchCell->Values[0] = pContentCell->Value;
					pContentCell->Value = branchOffset;
				}
				else
				{
					BranchPage* pBranchPage = pBranchPages[lastBranchOffset >> 16];
					if (!pBranchPage)
					{
						pBranchPage = new BranchPage();
						pBranchPages[BranchPagesCount++] = pBranchPage;

						if (BranchPagesCount == BranchPagesSize)
						{
							reallocateBranchPages();
						}
					}

					pBranchCell = &pBranchPage->pBranch[lastBranchOffset & 0xFFFF];

					pBranchCell->Values[0] = pContentCell->Value;
					pContentCell->Value = lastBranchOffset++;
				}

				pBranchCell->Offsets[0] = contentOffset + 1;

				pBranchCell->Values[1] = keyValue;
				pSetLastContentOffset = &pBranchCell->Offsets[1];

				goto FILL_KEY;
			}
		}
		else if (pContentCell->Type <= MAX_HEADER_BLOCK_TYPE) //HEADER BLOCK ==========================================================
		{
			uchar8 parentID = pContentCell->Value >> 24; //0..63
			uint32 headerBaseOffset = pContentCell->Value & 0xFFFFFF;

			uint32 headerOffset = 0;

			switch (pContentCell->Type - MIN_HEADER_BLOCK_TYPE)
			{
			default:
				break;
			};

			ContentCell& headerCell = pHeader[headerBaseOffset + headerOffset];

			switch (headerCell.Type)
			{
			case EMPTY_TYPE: //fill header cell
			{
				headerCell.Type = HEADER_CURRENT_VALUE_TYPE << 6 | parentID;
				pSetLastContentOffset = &headerCell.Value;

				goto FILL_KEY2;
			}
			case HEADER_JUMP_TYPE: //create header branch
			{
				HeaderBranchPage* pHeaderBranchPage = pHeaderBranchPages[lastHeaderBranchOffset >> 16];
				if (!pHeaderBranchPage)
				{
					pHeaderBranchPage = new HeaderBranchPage();
					pHeaderBranchPages[HeaderBranchPagesCount++] = pHeaderBranchPage;

					if (HeaderBranchPagesCount == HeaderBranchPagesSize)
					{
						reallocateHeaderBranchPages();
					}
				}

				HeaderBranchCell& headerBranchCell = pHeaderBranchPage->pHeaderBranch[lastHeaderBranchOffset & 0xFFFF];
				headerBranchCell.HeaderOffset = headerCell.Value;

				headerBranchCell.ParentIDs[0] = parentID;
				pSetLastContentOffset = &headerBranchCell.Offsets[0];

				headerCell.Type = HEADER_BRANCH_TYPE;
				headerCell.Value = lastBranchOffset++;

				goto FILL_KEY2;
			}
			case HEADER_BRANCH_TYPE: //header branch, check
			{
				HeaderBranchCell* pHeaderBranchCell = &pHeaderBranchPages[headerCell.Value >> 16]->pHeaderBranch[headerCell.Value & 0xFFFF];

				while (true)
				{
					for (uint32 i = 0; i < BRANCH_ENGINE_SIZE; i++)
					{
						if (pHeaderBranchCell->ParentIDs[i] == parentID) //exists way, continue
						{
							contentOffset = pHeaderBranchCell->Offsets[i];

							goto NEXT_KEY_PART;
						}
						else if (pHeaderBranchCell->ParentIDs[i] == 0) //new way
						{
							pHeaderBranchCell->ParentIDs[i] = parentID;
							pSetLastContentOffset = &pHeaderBranchCell->Offsets[i];

							goto FILL_KEY2;
						}
					}

					if (pHeaderBranchCell->pNextHeaderBranhCell)
					{
						pHeaderBranchCell = pHeaderBranchCell->pNextHeaderBranhCell;
					}
					else
					{
						break;
					}
				}

				//create new branch
				HeaderBranchPage* pHeaderBranchPage = pHeaderBranchPages[lastHeaderBranchOffset >> 16];
				if (!pHeaderBranchPage)
				{
					pHeaderBranchPage = new HeaderBranchPage();
					pHeaderBranchPages[HeaderBranchPagesCount++] = pHeaderBranchPage;

					if (HeaderBranchPagesCount == HeaderBranchPagesSize)
					{
						reallocateHeaderBranchPages();
					}
				}

				pHeaderBranchCell->pNextHeaderBranhCell = &pHeaderBranchPage->pHeaderBranch[lastHeaderBranchOffset & 0xFFFF];

				pHeaderBranchCell->pNextHeaderBranhCell->ParentIDs[0] = parentID;
				pSetLastContentOffset = &pHeaderBranchCell->pNextHeaderBranhCell->Offsets[0];

				goto FILL_KEY2;
			}
			default: //HEADER_CURRENT_VALUE_TYPE
			{
				uchar8 currParentID = headerCell.Type << 2 >> 2;

				if (parentID == currParentID) //our header cell, continue
				{
					contentOffset = headerCell.Value;

					goto NEXT_KEY_PART;
				}
				else //create header branch
				{
					HeaderBranchPage* pHeaderBranchPage = pHeaderBranchPages[lastHeaderBranchOffset >> 16];
					if (!pHeaderBranchPage)
					{
						pHeaderBranchPage = new HeaderBranchPage();
						pHeaderBranchPages[HeaderBranchPagesCount++] = pHeaderBranchPage;

						if (HeaderBranchPagesCount == HeaderBranchPagesSize)
						{
							reallocateHeaderBranchPages();
						}
					}

					HeaderBranchCell& headerBranchCell = pHeaderBranchPage->pHeaderBranch[lastHeaderBranchOffset & 0xFFFF];

					//existing
					headerBranchCell.ParentIDs[0] = currParentID;
					headerBranchCell.Offsets[0] = headerCell.Value;

					//our new
					headerBranchCell.ParentIDs[1] = parentID;
					pSetLastContentOffset = &headerBranchCell.Offsets[1];

					headerCell.Type = HEADER_BRANCH_TYPE;
					headerCell.Value = lastBranchOffset++;

					goto FILL_KEY2;
				}
			}
			}
		}

	FILL_KEY:

		keyOffset++;

	FILL_KEY2:

		uint32 restKeyLen = keyLen - keyOffset;

		if (tailReleasedContentOffsets[restKeyLen])
		{
			uint32 startContentOffset = *pSetLastContentOffset = tailReleasedContentOffsets[restKeyLen];

			//fill key
			pContentPage = pContentPages[startContentOffset >> 16];
			contentIndex = startContentOffset & 0xFFFF;

			pContentPage->pContent[contentIndex].Type = ONLY_CONTENT_TYPE + restKeyLen;
			tailReleasedContentOffsets[restKeyLen] = pContentPage->pContent[contentIndex].Value;

			countReleasedContentCells -= (restKeyLen + ValueLen);

			for (; keyOffset < keyLen; contentIndex++, keyOffset++)
			{
				pContentPage->pContent[contentIndex].Value = key[keyOffset];
			}

			pContentPage->pContent[contentIndex].Type = VALUE_TYPE;
			pContentPage->pContent[contentIndex].Value = value;

			return 0;
		}
		else
		{
			pContentPage = pContentPages[lastContentOffset >> 16];
			contentIndex = lastContentOffset & 0xFFFF;

			if (contentIndex > maxSafeShort) //content in next page
			{
				//release content cells
				uint32 spaceLen = MAX_SHORT - contentIndex - 1;

				pContentPage->pContent[contentIndex].Value = tailReleasedContentOffsets[spaceLen];

				tailReleasedContentOffsets[spaceLen] = lastContentOffset;

				countReleasedContentCells += (spaceLen + ValueLen);

				//move to next page
				uint32 page = (lastContentOffset >> 16) + 1;

				pContentPage = pContentPages[page];

				lastContentOffset = (page << 16);

				contentIndex = 0;
			}

			//set last content offset in cell
			*pSetLastContentOffset = lastContentOffset;

			//fill key
			pContentPage->pContent[contentIndex].Type = ONLY_CONTENT_TYPE + restKeyLen;

			for (; keyOffset < keyLen; contentIndex++, keyOffset++, lastContentOffset++)
			{
				pContentPage->pContent[contentIndex].Value = key[keyOffset];
			}

			pContentPage->pContent[contentIndex].Type = VALUE_TYPE;
			pContentPage->pContent[contentIndex].Value = value;

			lastContentOffset++;

			return 0;
		}
	}
	catch (...)
	{
		destroy();

		throw;
	}
}
