// Copyright 2020 fpwong, Inc. All Rights Reserved.

#include "FocusSearchBoxMenu.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintAssistUtils.h"

#include "K2Node_Event.h"
#include "BlueprintEditor.h"
#include "BlueprintAssistGraphHandler.h"

void SFocusSearchBoxMenu::Construct(
	const FArguments& InArgs,
	TSharedPtr<FBAInputProcessor> InEditor)
{
	SuggestionIndex = INDEX_NONE;

	AllItems.Empty();

	TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();

	TArray<TSharedPtr<SWidget>> DockTabs;
	FBAUtils::GetChildWidgets(Window, "SDockTab", DockTabs);

	for (TSharedPtr<SWidget> Widget : DockTabs)
	{
		TSharedPtr<SDockTab> DockTab = StaticCastSharedPtr<SDockTab>(Widget);

		if (DockTab.IsValid())
		{
			if (DockTab->GetTabRole() == ETabRole::MajorTab)
				continue;

			if (!DockTab->IsForeground())
				continue;

			TArray<TSharedPtr<SWidget>> SearchBoxes;
			FBAUtils::GetChildWidgets(DockTab->GetContent(), "SSearchBox", SearchBoxes);

			for (TSharedPtr<SWidget> SearchBoxWidget : SearchBoxes)
			{
				if (SearchBoxWidget->GetVisibility().IsVisible() &&
					SearchBoxWidget->IsEnabled() &&
					SearchBoxWidget->GetDesiredSize().SizeSquared() > 0 &&
					SearchBoxWidget->GetCachedGeometry().GetAbsoluteSize().SizeSquared() > 0)
				{
					AllItems.Add(MakeShareable(new FSearchBoxStruct(SearchBoxWidget, DockTab)));
				}
			}
		}
	}

	FilteredItems = AllItems;

	const FSlateFontInfo FontInfo =
		FEditorStyle::GetFontStyle(FName("BlueprintEditor.ActionMenu.ContextDescriptionFont"));

	SBorder::Construct(
		SBorder::FArguments()
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(400)
			.HeightOverride(400)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 5)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString("Focus search box")))
					.Font(FontInfo)
					.WrapTextAt(280)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(FilterTextBox, SSearchBox)
					.OnTextChanged(this, &SFocusSearchBoxMenu::OnFilterTextChanged)
					.OnTextCommitted(this, &SFocusSearchBoxMenu::OnFilterTextCommitted)
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(FilteredItemsListView, SListView<TSharedPtr<FSearchBoxStruct>>)
					.ItemHeight(24)
					.SelectionMode(ESelectionMode::Single)
					.ListItemsSource(&FilteredItems)
					.OnGenerateRow(this, &SFocusSearchBoxMenu::CreateItemWidget)
					.OnMouseButtonClick(this, &SFocusSearchBoxMenu::OnListItemClicked)
					.IsFocusable(false)
				]
			]
		]
	);

	FilterTextBox->SetOnKeyDownHandler(FOnKeyDown::CreateSP(this, &SFocusSearchBoxMenu::OnKeyDown));

	FSlateApplication::Get().SetKeyboardFocus(FilterTextBox.ToSharedRef());
}

TSharedRef<ITableRow> SFocusSearchBoxMenu::CreateItemWidget(
	TSharedPtr<FSearchBoxStruct> Item,
	const TSharedRef<STableViewBase>& OwnerTable) const
{
	return
		SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Left).FillWidth(1)
			[
				SNew(STextBlock).Text(FText::FromString(Item->ToString()))
			]
			+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Left).FillWidth(1)
			[
				SNew(STextBlock).Text(FText::FromString(Item->GetTabLabel()))
			]
		];
}

void SFocusSearchBoxMenu::OnListItemClicked(TSharedPtr<FSearchBoxStruct> Item)
{
	SelectItem(Item);
}

void SFocusSearchBoxMenu::OnFilterTextChanged(const FText& InFilterText)
{
	// Trim and sanitized the filter text (so that it more likely matches the action descriptions)
	const FString TrimmedFilterString = FText::TrimPrecedingAndTrailing(InFilterText).ToString();

	// Tokenize the search box text into a set of terms; all of them must be present to pass the filter
	TArray<FString> FilterTerms;
	TrimmedFilterString.ParseIntoArray(FilterTerms, TEXT(" "), true);

	FilteredItems.Empty();

	const bool bRequiresFiltering = FilterTerms.Num() > 0;
	for (int32 PinIndex = 0; PinIndex < AllItems.Num(); ++PinIndex)
	{
		TSharedPtr<FSearchBoxStruct> Item = AllItems[PinIndex];

		// If we're filtering, search check to see if we need to show this action
		bool bShowAction = true;
		if (bRequiresFiltering)
		{
			const FString& SearchText = Item->ToString() + " " + Item->GetTabLabel();

			FString EachTermSanitized;
			for (int32 FilterIndex = 0; FilterIndex < FilterTerms.Num() && bShowAction; ++
			     FilterIndex)
			{
				const bool bMatchesTerm = SearchText.Contains(FilterTerms[FilterIndex]);
				bShowAction = bShowAction && bMatchesTerm;
			}
		}

		if (bShowAction)
		{
			FilteredItems.Add(Item);
		}
	}

	FilteredItemsListView->RequestListRefresh();

	// Make sure the selected suggestion stays within the filtered list
	if (SuggestionIndex >= 0 && FilteredItems.Num() > 0)
	{
		//@TODO: Should try to actually maintain the highlight on the same item if it survived the filtering
		SuggestionIndex = FMath::Clamp<int32>(SuggestionIndex, 0, FilteredItems.Num() - 1);
		MarkActiveSuggestion();
	}
	else
	{
		SuggestionIndex = INDEX_NONE;
	}
}

void SFocusSearchBoxMenu::OnFilterTextCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		SelectFirstItem();
	}
}

void SFocusSearchBoxMenu::SelectItem(TSharedPtr<FSearchBoxStruct> Item)
{
	FSlateApplication::Get().DismissMenuByWidget(SharedThis(this));

	if (Item->Widget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(Item->Widget, EFocusCause::Navigation);
		Item->DockTab->FlashTab();
	}
}

bool SFocusSearchBoxMenu::SelectFirstItem()
{
	if (FilteredItems.Num() > 0)
	{
		SelectItem(FilteredItems[0]);
		return true;
	}

	return false;
}

void SFocusSearchBoxMenu::MarkActiveSuggestion()
{
	if (SuggestionIndex >= 0)
	{
		TSharedPtr<FSearchBoxStruct>& ItemToSelect = FilteredItems[SuggestionIndex];
		FilteredItemsListView->SetSelection(ItemToSelect);
		FilteredItemsListView->RequestScrollIntoView(ItemToSelect);

		ItemToSelect->DockTab->FlashTab();
	}
	else
	{
		FilteredItemsListView->ClearSelection();
	}
}

FReply SFocusSearchBoxMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	int32 SelectionDelta = 0;

	const int NumItems = FilteredItems.Num();

	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		FSlateApplication::Get().DismissMenuByWidget(SharedThis(this));
		return FReply::Handled();
	}

	if (KeyEvent.GetKey() == EKeys::Enter)
	{
		TArray<TSharedPtr<FSearchBoxStruct>> SelectedItems;
		FilteredItemsListView->GetSelectedItems(SelectedItems);

		if (SelectedItems.Num() > 0)
		{
			SelectItem(SelectedItems[0]);
			return FReply::Handled();
		}

		return SelectFirstItem() ? FReply::Handled() : FReply::Unhandled();
	}

	if (NumItems > 0)
	{
		// move up and down through the filtered node list
		if (KeyEvent.GetKey() == EKeys::Up)
		{
			SelectionDelta = -1;
		}
		else if (KeyEvent.GetKey() == EKeys::Down)
		{
			SelectionDelta = +1;
		}

		if (SelectionDelta != 0)
		{
			// If we have no selected suggestion then we need to use the items in the root to set the selection and set the focus
			if (SuggestionIndex == INDEX_NONE)
			{
				SuggestionIndex = (SuggestionIndex + SelectionDelta + NumItems) % NumItems;
				MarkActiveSuggestion();
				return FReply::Handled();
			}

			//Move up or down one, wrapping around
			SuggestionIndex = (SuggestionIndex + SelectionDelta + NumItems) % NumItems;
			MarkActiveSuggestion();

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

TSharedRef<SEditableTextBox> SFocusSearchBoxMenu::GetFilterTextBox()
{
	return FilterTextBox.ToSharedRef();
}

/********************/
/* FSearchBoxStruct */
/********************/

FString FSearchBoxStruct::ToString() const
{
	TSharedPtr<SWidget> FoundWidget = FBAUtils::GetChildWidget(Widget, "SEditableText");
	TSharedPtr<SEditableText> EditableTextBox = StaticCastSharedPtr<SEditableText>(FoundWidget);
	if (EditableTextBox.IsValid())
		return EditableTextBox->GetHintText().ToString();

	return Widget->ToString();
}

FString FSearchBoxStruct::GetTabLabel() const
{
	return DockTab->GetTabLabel().ToString();
}
