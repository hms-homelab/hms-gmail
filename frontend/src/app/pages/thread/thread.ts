import { Component, inject, signal, OnInit } from '@angular/core';
import { ActivatedRoute, Router } from '@angular/router';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { GmailService } from '../../services/gmail.service';
import { SearchResult } from '../../models/email.model';

@Component({
  selector: 'app-thread',
  standalone: true,
  imports: [CommonModule, MatButtonModule, MatIconModule, MatProgressSpinnerModule],
  template: `
    <div class="page-container">
      <div class="back-row">
        <button mat-button (click)="back()">
          <mat-icon>arrow_back</mat-icon> Back
        </button>
      </div>

      <div *ngIf="loading()" class="loading-center">
        <mat-spinner diameter="40"></mat-spinner>
      </div>

      <div *ngIf="!loading() && emails().length > 0" class="thread-header card">
        <div class="thread-subject">{{ emails()[0].subject || '(no subject)' }}</div>
        <div class="thread-count">{{ emails().length }} messages</div>
      </div>

      <div *ngFor="let e of emails(); let i = index" class="card thread-email" (click)="open(e)">
        <div class="te-header">
          <span class="te-from">{{ e.from }}</span>
          <span class="te-date">{{ e.date | date:'MMM d, y HH:mm' }}</span>
        </div>
        <div class="te-snippet">{{ e.snippet }}</div>
        <mat-icon *ngIf="e.has_attachment" class="attach-icon">attach_file</mat-icon>
      </div>

      <div *ngIf="!loading() && emails().length === 0" class="empty-msg">
        Thread not found.
      </div>
    </div>
  `,
  styles: [`
    .back-row { margin-bottom: 12px; }
    .loading-center { display: flex; justify-content: center; padding: 40px; }
    .thread-header { }
    .thread-subject { font-size: 18px; font-weight: 500; }
    .thread-count { font-size: 13px; color: #888; margin-top: 4px; }
    .thread-email { cursor: pointer; &:hover { background: #2a2a2a; } }
    .te-header { display: flex; justify-content: space-between; margin-bottom: 4px; }
    .te-from { font-size: 14px; font-weight: 500; }
    .te-date { font-size: 12px; color: #888; }
    .te-snippet { font-size: 13px; color: #999; }
    .attach-icon { font-size: 16px; color: #888; margin-top: 4px; }
    .empty-msg { text-align: center; color: #aaa; padding: 40px; }
  `]
})
export class ThreadPage implements OnInit {
  private route = inject(ActivatedRoute);
  private router = inject(Router);
  private gmail = inject(GmailService);

  emails = signal<SearchResult[]>([]);
  loading = signal(true);

  ngOnInit() {
    const tid = this.route.snapshot.paramMap.get('tid') ?? '';
    this.gmail.getThread(tid).subscribe({
      next: (res) => {
        this.emails.set(res.emails);
        this.loading.set(false);
      },
      error: () => this.loading.set(false)
    });
  }

  back() { history.back(); }
  open(e: SearchResult) { this.router.navigate(['/emails', e.id]); }
}
